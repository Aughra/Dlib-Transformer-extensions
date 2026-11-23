#!/usr/bin/env python3
# La justesse de la recherche dans les documents (RAG) de Gulliver (fork pour macOS, 2026-11-22).
#
#   python3 tools/test_rag.py [--json sortie.json] [--generate] [--models DOSSIER]
#
# Le corpus : les épisodes du blog du portage (blog/NN-*.md).  Les questions (tools/rag_questions.json) ont été écrites
# une fois par Ministral 3 8B à partir de passages tirés au hasard (--generate), puis figées : le test est le
# même chaque semaine.  Chaque question garde son épisode et un extrait du passage qui y répond.
#
# Par le vrai service, mais un service d'essai isolé (port 18495, ses propres données dans build/rag-test, hors ligne,
# effacées à la fin), jamais celui de l'utilisateur : il reçoit par sa bibliothèque un clone (cp -c : instantané, sans
# place prise) du modèle de recherche installé (qwen3-embedding-0.6b.gguf, pris dans --models, par défaut
# build/gulliver-home/models), et de Ministral 3 8B pour --generate.  Dans le profil de son administrateur, les épisodes
# sont indexés comme la page le fait (POST /gulliver/v1/profile/documents/<doc>/index : découpage et vecteurs par le
# service), puis chaque question est cherchée (POST /gulliver/v1/profile/search, son vecteur par /v1/embeddings avec la
# consigne de Qwen3-Embedding, comme web/js/rag.js) avec chaque moteur (hybrid, dense, lexical), avec et sans le tri
# fin, et 6, 12 ou 20 passages (le budget de la page : 1 500 caractères par passage).  Une question est trouvée
# quand un des extraits rendus contient son extrait.  Mesures :
# le rappel (la part des questions trouvées) et le rang moyen réciproque (1 si le bon passage est premier, 1/2 s'il est
# deuxième…).
import argparse
import glob
import json
import os
import random
import re
import shutil
import signal
import ssl
import subprocess
import sys
import time
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from faux_smtp import FakeSMTP  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "../../../../.."))
QUESTIONS = os.path.join(HERE, "rag_questions.json")
WORK = os.path.join(ROOT, "build", "rag-test")
SERVICE = os.path.join(ROOT, "build", "macos-gulliver", "bin", "gulliver-service")
PORT = 18495
EMBEDDER, WRITER = "qwen3-embedding-0.6b.gguf", "ministral-3-8b.gguf"
CTX = ssl.create_default_context()
CTX.check_hostname = False
CTX.verify_mode = ssl.CERT_NONE


class Service:
    """Le service d'essai : ses données, ses modèles clonés, son lancement et son arrêt."""

    def __init__(self, models_dir, files):
        self.base = f"https://127.0.0.1:{PORT}"
        self.home = os.path.join(WORK, "home")
        self.token = ""
        shutil.rmtree(WORK, ignore_errors=True)
        library = os.path.join(self.home, "library")
        os.makedirs(library, mode=0o700)
        os.chmod(self.home, 0o700)
        for f in files:
            subprocess.run(["cp", "-c", os.path.join(models_dir, f), library], check=True)
        with open(os.path.join(self.home, "settings.json"), "w") as f:
            json.dump({"port": PORT, "offline": True, "default_model": False, "lan": False, "library_dir": library}, f)
        self.log = open(os.path.join(WORK, "service.log"), "a")
        self.proc = subprocess.Popen([SERVICE, "--console"], env=dict(os.environ, GULLIVER_HOME=self.home),
                                     stdout=self.log, stderr=self.log)
        try:
            self.prepare(files)
        except BaseException:
            self.stop()
            raise

    def prepare(self, files):
        """Attend le service, installe les modèles de la bibliothèque et les active."""
        for _ in range(120):
            try:
                self.call("GET", "/gulliver/v1/status", timeout=3)
                break
            except Exception:
                time.sleep(1)
        else:
            raise RuntimeError("the test service does not answer")
        self.ids = {}
        for f in files:
            r = self.call("POST", "/gulliver/v1/library/load", {"file": f})
            wanted = (r.get("imported") or []) + (r.get("already") or [])
            for _ in range(120):
                # Les modèles de recherche sont rangés à part (embedding_models), sans le champ « installed ».
                listing = self.call("GET", "/gulliver/v1/models")
                found = [m for m in listing.get("models", [])
                         if m.get("installed") and (m.get("id") in wanted or os.path.basename(m.get("file") or "") == f)]
                found += [m for m in listing.get("embedding_models", []) if m.get("id") in wanted]
                if found:
                    self.ids[f] = found[0]["id"]
                    break
                time.sleep(1)
            else:
                raise RuntimeError("the library did not load " + f)
        if WRITER in self.ids:
            self.call("POST", f"/gulliver/v1/models/{self.ids[WRITER]}/activate", {"role": "general"})
        active = self.call("GET", "/gulliver/v1/models").get("active") or {}
        if active.get("embedding") != self.ids.get(EMBEDDER):
            self.call("POST", f"/gulliver/v1/models/{self.ids[EMBEDDER]}/activate", {"role": "embedding"})

    def stop(self):
        self.proc.send_signal(signal.SIGTERM)
        try:
            self.proc.wait(30)
        except subprocess.TimeoutExpired:
            self.proc.kill()
        shutil.rmtree(WORK, ignore_errors=True)

    def call(self, method, path, body=None, timeout=300, raw=False):
        data = json.dumps(body).encode() if body is not None else None
        headers = {"Content-Type": "application/json"}
        if self.token:
            headers["X-Gulliver-Session"] = self.token
        req = urllib.request.Request(self.base + path, data=data, method=method, headers=headers)
        with urllib.request.urlopen(req, context=CTX, timeout=timeout) as r:
            return r.read().decode() if raw else json.load(r)


def corpus():
    """Les épisodes du blog : (identifiant, nom, texte)."""
    out = []
    for path in sorted(glob.glob(os.path.join(ROOT, "blog", "[0-9][0-9]-*.md"))):
        name = os.path.basename(path)
        out.append((re.sub(r"[^a-z0-9-]", "-", name[:-3].lower()), name, open(path, encoding="utf-8").read()))
    return out


def paragraphs(text):
    """Les paragraphes de prose d'un épisode (ni titres, ni tableaux, ni code), de 400 à 1 200 caractères."""
    text = re.sub(r"```.*?```", "", text, flags=re.S)
    keep = []
    for p in re.split(r"\n\s*\n", text):
        p = p.strip()
        if 400 <= len(p) <= 1200 and not p.startswith(("#", "|", ">", "-", "*")):
            keep.append(p)
    return keep


def snippet(p):
    """Un extrait du milieu du paragraphe (60 caractères), qui sert à reconnaître le bon passage."""
    mid = len(p) // 2
    s = p[max(0, mid - 30):mid + 30]
    return s.strip()


def generate(service, n=40, seed=7):
    """Les questions : n paragraphes tirés au hasard, une question écrite par le modèle pour chacun."""
    rng = random.Random(seed)
    pool = [(doc, name, p) for doc, name, text in corpus() for p in paragraphs(text)]
    rng.shuffle(pool)
    out = []
    for doc, name, p in pool:
        if len(out) >= n:
            break
        prompt = ("Voici un passage d'un blog :\n\n" + p + "\n\nÉcris une seule question en français, précise, à laquelle "
                  "ce passage répond, comme la poserait un lecteur qui ne l'a pas sous les yeux. N'emploie pas les "
                  "mêmes tournures que le passage. Réponds par la question seule, sans guillemets.")
        r = service.call("POST", "/v1/chat/completions", {"messages": [{"role": "user", "content": prompt}],
                                                          "max_tokens": 80, "temperature": 0}, timeout=600)
        q = (r.get("choices") or [{}])[0].get("message", {}).get("content", "").strip().splitlines()
        q = q[-1].strip(" «»\"") if q else ""
        if q.endswith("?") and 15 <= len(q) <= 300:
            out.append({"question": q, "doc": doc, "name": name, "snippet": snippet(p)})
            print(f"{len(out):2d}. {q}  [{name}]", flush=True)
    json.dump({"generated": time.strftime("%Y-%m-%d %H:%M"), "seed": seed, "questions": out},
              open(QUESTIONS, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--models", default=os.path.join(ROOT, "build", "gulliver-home", "models"))
    ap.add_argument("--json")
    ap.add_argument("--generate", action="store_true")
    a = ap.parse_args()
    generating = a.generate or not os.path.exists(QUESTIONS)
    service = Service(a.models, [EMBEDDER, WRITER] if generating else [EMBEDDER])
    try:
        if generating:
            generate(service)
        out = evaluate(service, json.load(open(QUESTIONS, encoding="utf-8"))["questions"])
    finally:
        service.stop()
    if a.json:
        json.dump(out, open(a.json, "w", encoding="utf-8"), ensure_ascii=False, indent=1)


def evaluate(service, questions):
    embedder = (service.call("GET", "/gulliver/v1/models").get("active") or {}).get("embedding", "")
    # Le service d'essai est neuf : son premier profil, l'administrateur, choisit un mot de passe tiré au hasard par
    # le lien du mail de son premier compte (comptes, Gulliver 6.31.0 ; par mail depuis le 2026-11-23, un faux
    # facteur, tools/faux_smtp.py, gardant le mail) ; sa session sert pour tout le test.
    smtp = FakeSMTP()
    try:
        service.call("PUT", "/gulliver/v1/settings", {"mail_from": "gulliver@essai.invalid", "mail_port": smtp.port})
        service.call("POST", "/gulliver/v1/setup", {"name": "essai-rag", "email": "rag@essai.invalid"})
        service.token = service.call("POST", "/gulliver/v1/password/reset", {
            "token": smtp.reset_for("rag@essai.invalid"), "password": os.urandom(16).hex()})["token"]
    finally:
        smtp.close()
    results = {}
    docs = corpus()
    t = time.time()
    for doc, docname, text in docs:
        body = service.call("POST", f"/gulliver/v1/profile/documents/{doc}/index",
                            {"owner": "essai", "name": docname, "text": text}, timeout=900, raw=True)
        last = [l for l in body.splitlines() if l.startswith("data: {")]
        info = json.loads(last[-1][6:]) if last else {}
        if info.get("error") or not info.get("vectors"):
            raise RuntimeError(f"indexing {docname}: {body[-200:]}")
    index_s = time.time() - t
    print(f"{len(docs)} épisodes indexés en {index_s:.0f} s", flush=True)
    vectors = {}
    for q in questions:
        text = q["question"]
        if re.search(r"qwen3-embedding", embedder, re.I):
            text = "Instruct: Given a question, retrieve the passages of the documents that answer it\nQuery:" + text
        r = service.call("POST", "/v1/embeddings", {"model": embedder, "input": [text], "encoding_format": "base64"})
        vectors[q["question"]] = r["data"][0]["embedding"]
    for engine in ("hybrid", "dense", "lexical"):
        for rerank in (False, True):
            ranks = []
            for q in questions:
                r = service.call("POST", "/gulliver/v1/profile/search", {
                    "question": q["question"], "owners": ["essai"], "engine": engine, "rerank": rerank, "model": embedder,
                    "question_vector": vectors[q["question"]] if engine != "lexical" else "", "limit": 20,
                    "budget": 1500 * 20})
                rank = None
                for i, e in enumerate(r.get("excerpts") or []):
                    if q["snippet"] in (e.get("text") or ""):
                        rank = i + 1
                        break
                ranks.append(rank)
            key = engine + (" + tri fin" if rerank else "")
            res = {f"rappel_{k}": round(sum(1 for x in ranks if x and x <= k) / len(ranks), 3) for k in (6, 12, 20)}
            res["rang_reciproque"] = round(sum(1 / x for x in ranks if x) / len(ranks), 3)
            results[key] = res
            print(f"{key:20s} rappel@6 {res['rappel_6']:.2f}  @12 {res['rappel_12']:.2f}  @20 {res['rappel_20']:.2f}"
                  f"  rang réciproque {res['rang_reciproque']:.2f}", flush=True)
    return {"date": time.strftime("%Y-%m-%d %H:%M"), "questions": len(questions), "documents": len(docs),
            "embedder": embedder, "index_s": round(index_s, 1), "results": results}


if __name__ == "__main__":
    main()
