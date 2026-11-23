#!/usr/bin/env python3
# Les mesures « comme un utilisateur » (fork pour macOS, 2026-11-22) : par le vrai service, sur un service d'essai isolé,
# jamais sur le service de l'utilisateur.
#
#   python3 tools/bench_service.py fichier.gguf [--name "nom affiché"] [--json sortie.json] [--llama]
#
# Un service Gulliver d'essai (port 18496, ses propres données dans build/bench-service, hors ligne) reçoit un clone du
# modèle par sa bibliothèque (cp -c : instantané, sans place prise), puis :
#   5. le temps de chargement : du lancement du service, le modèle déjà installé, à la fin d'une toute petite réponse
#      (llama-server : de son lancement à la même réponse) ;
#   1. une conversation longue : un document d'environ 32 768 tokens, une réponse de 512 tokens au plus, avec les
#      brouillons (le réglage par défaut), puis le service relancé sans brouillons (draft_tokens 0) ;
#   2. 4 requêtes à la fois (questions courtes, 256 tokens de réponse chacune) : le débit total ;
#   3. 3 documents d'environ 4 000 tokens à la fois (256 tokens de réponse) : le temps total et l'attente moyenne ;
#   4. le plus grand contexte qui tient : 65 536 puis 131 072 tokens (dans la limite du modèle, 95 % de la taille pour
#      laisser la place du modèle de conversation), réponse de 16 tokens, jusqu'au premier échec ; 262 144 n'est pas
#      essayé (de 30 à 60 minutes de lecture chaque semaine).
# Avec --llama, les tests 5, 1 (sans brouillons), 2 et 3 sur llama-server (llama.cpp, 4 requêtes en parallèle, même
# fichier), comme une ligne de comparaison.  Les temps viennent des réponses elles-mêmes (« timings », « usage ») ;
# chaque requête a un temps limite.  Écrit le résultat en JSON (--json) et un résumé sur la sortie.
import argparse
import concurrent.futures
import json
import os
import shutil
import signal
import ssl
import subprocess
import time
import urllib.request

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../../../.."))
WORK = os.path.join(ROOT, "build", "bench-service")
SERVICE = os.path.join(ROOT, "build", "macos-gulliver", "bin", "gulliver-service")
TEXT = os.path.join(ROOT, "build", "bench-moteurs", "texte.txt")
PORT, LLAMA_PORT = 18496, 18497
CTX = ssl.create_default_context()
CTX.check_hostname = False
CTX.verify_mode = ssl.CERT_NONE


def say(text):
    print(time.strftime("%H:%M:%S ") + text, flush=True)


def call(base, path, body=None, method=None, timeout=60):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(base + path, data=data, method=method or ("POST" if data else "GET"),
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, context=CTX if base.startswith("https") else None, timeout=timeout) as r:
        return json.load(r)


def chat(base, text, max_tokens, timeout=900):
    """Une requête ; rend (temps total, usage, timings) ou une exception."""
    t = time.time()
    r = call(base, "/v1/chat/completions", {"messages": [{"role": "user", "content": text}], "max_tokens": max_tokens,
                                            "temperature": 0}, timeout=timeout)
    return time.time() - t, r.get("usage") or {}, r.get("timings") or {}


def speeds(usage, timings):
    """(lecture, écriture) en tokens/s, de Gulliver (prompt_per_second…) ou de llama-server (predicted_per_second…)."""
    read = timings.get("prompt_per_second")
    write = timings.get("generation_per_second", timings.get("predicted_per_second"))
    return read, write


class Gulliver:
    def __init__(self, model, drafts):
        self.base = f"https://127.0.0.1:{PORT}"
        self.home = os.path.join(WORK, "home")
        self.model, self.drafts, self.proc = model, drafts, None

    def start(self, fresh):
        if fresh:
            shutil.rmtree(WORK, ignore_errors=True)
            library = os.path.join(self.home, "library")
            os.makedirs(library, mode=0o700)
            os.chmod(self.home, 0o700)
            subprocess.run(["cp", "-c", self.model, library], check=True)
        # Au redémarrage, les réglages que le service a écrits (le modèle actif) sont gardés : seules nos clés changent.
        path = os.path.join(self.home, "settings.json")
        settings = {}
        if not fresh:
            try:
                settings = json.load(open(path))
            except (OSError, ValueError):
                settings = {}
        settings.update({"port": PORT, "offline": True, "default_model": False, "lan": False,
                         "library_dir": os.path.join(self.home, "library"), "draft_tokens": 7 if self.drafts else 0})
        with open(path, "w") as f:
            json.dump(settings, f)
        self.log = open(os.path.join(WORK, "service.log"), "a")
        self.proc = subprocess.Popen([SERVICE, "--console"], env=dict(os.environ, GULLIVER_HOME=self.home),
                                     stdout=self.log, stderr=self.log)
        for _ in range(120):
            try:
                call(self.base, "/gulliver/v1/status", timeout=3)
                return
            except Exception:
                time.sleep(1)
        raise RuntimeError("the test service does not answer")

    def stop(self):
        if self.proc:
            self.proc.send_signal(signal.SIGTERM)
            try:
                self.proc.wait(30)
            except subprocess.TimeoutExpired:
                self.proc.kill()
            self.proc = None

    def load(self):
        """Le modèle de la bibliothèque dans le service, actif ; rend le temps de chargement (test 5)."""
        name = os.path.basename(self.model)
        # La bibliothèque lit tout le fichier avant de répondre : une minute ne suffit pas à un modèle de 18 Go froid.
        r = call(self.base, "/gulliver/v1/library/load", {"file": name}, timeout=900)
        ids = (r.get("imported") or []) + (r.get("already") or [])
        mid = None
        for _ in range(120):
            models = call(self.base, "/gulliver/v1/models").get("models", [])
            found = [m for m in models if m.get("installed") and (m.get("id") in ids or os.path.basename(m.get("file") or "") == name)]
            if found:
                mid = found[0]["id"]
                break
            time.sleep(1)
        if not mid:
            raise RuntimeError("the library did not load " + name)
        # Le chargement : de l'activation à la fin d'une toute petite réponse ; le service ne sert un modèle qu'une fois
        # chargé (GET /gulliver/v1/models : « loaded »).
        t = time.time()
        call(self.base, f"/gulliver/v1/models/{mid}/activate", {"role": "general"})
        for _ in range(1200):
            if mid in (call(self.base, "/gulliver/v1/models").get("loaded") or []):
                break
            time.sleep(0.5)
        chat(self.base, "Bonjour.", 4, timeout=600)
        return time.time() - t


class Llama:
    def __init__(self, model):
        self.base = f"http://127.0.0.1:{LLAMA_PORT}"
        self.model, self.proc = model, None

    def start_and_load(self, ctx, slots):
        """llama-server : ctx tokens de contexte en tout, partagés entre slots places ; rend le temps jusqu'à la fin
        d'une toute petite réponse."""
        t = time.time()
        self.log = open(os.path.join(WORK, "llama-server.log"), "a")
        self.proc = subprocess.Popen(["llama-server", "-m", self.model, "-ngl", "99", "-c", str(ctx), "-np", str(slots),
                                      "--port", str(LLAMA_PORT), "--host", "127.0.0.1"], stdout=self.log, stderr=self.log)
        for _ in range(600):
            try:
                chat(self.base, "Bonjour.", 4, timeout=5)
                return time.time() - t
            except Exception:
                time.sleep(0.5)
        raise RuntimeError("llama-server does not answer")

    def stop(self):
        if self.proc:
            self.proc.send_signal(signal.SIGTERM)
            try:
                self.proc.wait(30)
            except subprocess.TimeoutExpired:
                self.proc.kill()
            self.proc = None


def document(base, tokens, offset=0):
    """Un texte d'environ tokens tokens (le texte de justesse, répété), mesuré par /gulliver/v1/count quand il existe."""
    src = open(TEXT, encoding="utf-8").read()
    src = src[offset % len(src):] + src[:offset % len(src)]
    ratio = 3.2
    try:
        n = call(base, "/gulliver/v1/count", {"text": src[:20000]}).get("tokens") or 0
        if n > 0:
            ratio = 20000 / n
    except Exception:
        pass
    chars = int(tokens * ratio)
    return (src * (chars // len(src) + 1))[:chars]


def long_conversation(base, count_base):
    doc = document(count_base, 32768 - 700)
    total, usage, timings = chat(base, "Voici un document :\n\n" + doc + "\n\nRésume ce document en détail, en reprenant "
                                       "ses chiffres.", 512)
    read, write = speeds(usage, timings)
    return {"prompt_tokens": usage.get("prompt_tokens"), "answer_tokens": usage.get("completion_tokens"),
            "read": read, "write": write, "total_s": round(total, 1)}


def together(base, count_base, n, doc_tokens, answer):
    texts = [("Voici un document :\n\n" + document(count_base, doc_tokens, 7919 * i) + "\n\nQue dit ce document ? "
              "Réponds en détail.") if doc_tokens else f"Explique en détail le sujet n° {i + 1} : la mémoire d'un "
              "ordinateur, ses niveaux et leur vitesse." for i in range(n)]
    t = time.time()
    with concurrent.futures.ThreadPoolExecutor(n) as pool:
        results = list(pool.map(lambda x: chat(base, x, answer), texts))
    wall = time.time() - t
    tokens = sum(u.get("completion_tokens") or 0 for _, u, _ in results)
    waits = [(tm.get("queued_ms", 0) + tm.get("prompt_ms", 0)) / 1000 for _, _, tm in results]
    return {"requests": n, "answered": len(results), "answer_tokens": tokens, "total_s": round(wall, 1),
            "total_write": round(tokens / wall, 1) if wall else None,
            "mean_first_token_s": round(sum(waits) / len(waits), 1) if waits and any(waits) else None}


def largest(base, count_base):
    try:
        limit = call(count_base, "/gulliver/v1/count", {"text": "x"}).get("context") or 262144
    except Exception:
        limit = 262144
    best = None
    for n in (65536, 131072):
        if n > limit:
            break
        try:
            _, usage, timings = chat(base, "Voici un document :\n\n" + document(count_base, int(n * 0.95)) + "\n\nEn une phrase, "
                                     "de quoi parle-t-il ?", 16, timeout=1200)
            best = {"context": n, "prompt_tokens": usage.get("prompt_tokens"), "read": speeds(usage, timings)[0]}
            say(f"  contexte de {n} : tient")
        except Exception as e:
            detail = str(e)
            try:
                detail += " " + e.read().decode(errors="replace")[:300]
            except Exception:
                pass
            say(f"  contexte de {n} : échec ({detail[:200]})")
            best = dict(best or {}, refused=n, reason=detail[:300])
            break
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model")
    ap.add_argument("--name", default="")
    ap.add_argument("--json")
    ap.add_argument("--llama", action="store_true")
    ap.add_argument("--quick", action="store_true", help="sans le plus grand contexte (pour un essai)")
    a = ap.parse_args()
    out = {"model": a.name or os.path.basename(a.model), "gulliver": {}, "llama": {}}
    g = Gulliver(a.model, True)
    try:
        g.start(True)
        say("Gulliver : installation du modèle")
        g.load()
        say("Gulliver : conversation de 32k, avec brouillons")
        out["gulliver"]["long_drafts"] = long_conversation(g.base, g.base)
        say("Gulliver : 4 requêtes à la fois")
        out["gulliver"]["four"] = together(g.base, g.base, 4, 0, 256)
        say("Gulliver : 3 documents de 4 000 tokens à la fois")
        out["gulliver"]["three_docs"] = together(g.base, g.base, 3, 4000, 256)
        if not a.quick:
            say("Gulliver : le plus grand contexte")
            out["gulliver"]["largest"] = largest(g.base, g.base)
        g.stop()
        # Le chargement (test 5) : du lancement du service, le modèle installé et actif, à une première réponse.
        g.drafts = False
        t = time.time()
        g.start(False)
        mid = (call(g.base, "/gulliver/v1/models").get("active") or {}).get("general")
        if not mid:
            raise RuntimeError("no active model after the restart of the test service")
        for _ in range(1200):
            if mid and mid in (call(g.base, "/gulliver/v1/models").get("loaded") or []):
                break
            time.sleep(0.5)
        chat(g.base, "Bonjour.", 4, timeout=600)
        out["gulliver"]["load_s"] = round(time.time() - t, 1)
        say(f"Gulliver : chargé en {out['gulliver']['load_s']} s ; conversation de 32k, sans brouillons")
        out["gulliver"]["long_plain"] = long_conversation(g.base, g.base)
    except Exception as e:
        out["gulliver"]["error"] = str(e)[:300]
        say("Gulliver : " + str(e)[:200])
    if a.llama and shutil.which("llama-server"):
        l = Llama(a.model)
        try:
            # Une seule place de 40 960 tokens pour la conversation longue (llama-server partage son contexte entre ses
            # places), puis 4 places de 8 192 pour les tests à plusieurs.
            say("llama.cpp : chargement")
            out["llama"]["load_s"] = round(l.start_and_load(40960, 1), 1)
            say("llama.cpp : conversation de 32k")
            out["llama"]["long_plain"] = long_conversation(l.base, g.base)
            l.stop()
            l.start_and_load(32768, 4)
            say("llama.cpp : 4 requêtes à la fois")
            out["llama"]["four"] = together(l.base, g.base, 4, 0, 256)
            say("llama.cpp : 3 documents de 4 000 tokens à la fois")
            out["llama"]["three_docs"] = together(l.base, g.base, 3, 4000, 256)
        except Exception as e:
            out["llama"]["error"] = str(e)[:300]
            say("llama.cpp : " + str(e)[:200])
        finally:
            l.stop()
    g.stop()
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump(out, f, ensure_ascii=False, indent=1)
    print(json.dumps(out, ensure_ascii=False, indent=1))


if __name__ == "__main__":
    main()
