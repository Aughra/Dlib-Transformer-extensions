#!/usr/bin/env python3
# Les modèles du mardi (fork pour macOS, 2026-11-22) : tenir à jour les modèles de Gulliver avant la mesure du dimanche.
#
#   python3 tools/modeles_mardi.py [GULLIVER_HOME] [--essai]
#
# Par le service qui tourne sur ce Mac (https://127.0.0.1:<port de ses réglages>, certificat du service accepté sans
# vérification : c'est le même Mac) :
#   1. le catalogue est rafraîchi (POST /gulliver/v1/models/refresh) ;
#   2. un modèle du catalogue installé dont le catalogue donne une autre empreinte que celle du fichier est
#      retéléchargé (sa nouvelle version) ;
#   3. un modèle ajouté depuis Hugging Face dont le fichier a changé sur Hugging Face (empreinte de l'en-tête
#      X-Linked-Etag, sans télécharger le fichier) est réajouté, ce qui télécharge sa nouvelle version ;
#   4. les modèles du catalogue qui tiennent dans ce Mac et qu'il ne possède pas encore sont téléchargés : une
#      architecture que le moteur lit, un fichier (avec son projecteur) d'au plus les trois quarts de la mémoire, un
#      modèle qui répond aux questions (pas un modèle de recherche dans les documents).
# Puis il attend la fin des téléchargements (six heures au plus).  Avec --essai, il ne fait que dire ce qu'il ferait.
# Les modèles ainsi installés entrent dans la mesure du dimanche (tools/modeles_installes.py).
import json
import os
import ssl
import subprocess
import sys
import time
import urllib.request

# Les architectures que le moteur lit (engine/model_config.cpp, read_model_config).
ARCHS = {"llama", "mistral", "mistral3", "granite", "qwen2", "qwen3", "qwen3vl", "mixtral", "granitemoe", "qwen3moe",
         "qwen3vlmoe", "qwen35moe", "phi3", "qwen35", "gemma4", "gemma3", "lfm2", "lfm2moe"}
RUNNING = {"queued", "downloading", "verifying"}


def say(text):
    print(time.strftime("%Y-%m-%d %H:%M ") + text, flush=True)


class Service:
    def __init__(self, home):
        port = 18443
        try:
            with open(os.path.join(home, "settings.json"), encoding="utf-8") as f:
                port = int(json.load(f).get("port", port))
        except (OSError, ValueError):
            pass
        self.base = f"https://127.0.0.1:{port}"
        self.context = ssl.create_default_context()
        self.context.check_hostname = False
        self.context.verify_mode = ssl.CERT_NONE

    def call(self, method, path, body=None):
        data = json.dumps(body).encode() if body is not None else (b"{}" if method == "POST" else None)
        req = urllib.request.Request(self.base + path, data=data, method=method,
                                     headers={"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, context=self.context, timeout=60) as r:
                return json.load(r)
        except urllib.error.HTTPError as e:
            try:
                return json.load(e)
            except ValueError:
                return {"ok": False, "code": str(e.code)}


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def hub_sha(url):
    """L'empreinte SHA-256 actuelle d'un fichier de Hugging Face (en-tête X-Linked-Etag), sans le télécharger."""
    opener = urllib.request.build_opener(NoRedirect)
    req = urllib.request.Request(url, method="HEAD", headers={"User-Agent": "gulliver-models-update"})
    try:
        r = opener.open(req, timeout=30)
        headers = r.headers
    except urllib.error.HTTPError as e:
        headers = e.headers
    except Exception:
        return None
    etag = (headers.get("X-Linked-Etag") or headers.get("ETag") or "").strip('"').strip("W/").strip('"')
    return etag if len(etag) == 64 else None


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    trial = "--essai" in sys.argv
    home = args[0] if args else os.environ.get("GULLIVER_HOME", "")
    service = Service(home)
    memory = int(subprocess.run(["sysctl", "-n", "hw.memsize"], capture_output=True, text=True).stdout.strip() or 0)
    budget = memory * 3 // 4
    if service.call("GET", "/gulliver/v1/models").get("offline"):
        say("le service est hors ligne : rien à faire")
        return
    if not trial:
        service.call("POST", "/gulliver/v1/models/refresh")
        for _ in range(60):
            time.sleep(5)
            if (service.call("GET", "/gulliver/v1/models").get("catalog") or {}).get("state") != "checking":
                break
    data = service.call("GET", "/gulliver/v1/models")
    say(f"catalogue : {(data.get('catalog') or {}).get('state', '?')}, {len(data.get('models', []))} modèles ; "
        f"mémoire {memory / 2**30:.0f} Go, au plus {budget / 2**30:.0f} Go par modèle")
    started = []
    for m in data.get("models", []):
        mid, size = m["id"], m.get("total_size") or m.get("size") or 0
        if m.get("origin") == "huggingface":
            if not m.get("installed") or not m.get("files"):
                continue
            url = m["files"][0].get("url", "")
            now = hub_sha(url.replace("/resolve/", "/resolve/", 1)) if "/resolve/" in url else None
            if now and now != m.get("sha256"):
                repo = m.get("source", "").replace("https://huggingface.co/", "")
                name = url.split("/resolve/", 1)[1].split("/", 1)[1] if "/resolve/" in url else ""
                say(f"{mid} : nouvelle version sur Hugging Face ({now[:12]}…), réajouté")
                if not trial:
                    r = service.call("POST", "/gulliver/v1/hub/add", {"repo": repo, "file": name,
                                                                       "projector": (m.get("projector") or {}).get("name", "")})
                    if r.get("ok"):
                        started.append(mid)
                    else:
                        say(f"{mid} : refusé ({r.get('code') or r.get('error')})")
            continue
        if m.get("installed"):
            if m.get("local_sha256") and m.get("sha256") and m["local_sha256"] != m["sha256"]:
                say(f"{mid} : nouvelle version dans le catalogue, retéléchargé")
                if not trial and service.call("POST", f"/gulliver/v1/models/{mid}/download").get("ok"):
                    started.append(mid)
            continue
        roles = m.get("roles") or []
        if m.get("arch") not in ARCHS or "embedding" in roles or not size or size > budget:
            why = ("architecture que le moteur ne lit pas" if m.get("arch") not in ARCHS else
                   "modèle de recherche dans les documents" if "embedding" in roles else "trop grand pour ce Mac")
            say(f"{mid} : laissé ({why})")
            continue
        say(f"{mid} : téléchargé ({size / 1e9:.1f} Go)")
        if not trial:
            r = service.call("POST", f"/gulliver/v1/models/{mid}/download")
            if r.get("ok"):
                started.append(mid)
            else:
                say(f"{mid} : refusé ({r.get('code')})")
    if trial or not started:
        say("rien à télécharger" if not started and not trial else "essai : rien n'a été lancé")
        return
    deadline = time.time() + 6 * 3600
    while time.time() < deadline:
        time.sleep(60)
        busy = [d for d in service.call("GET", "/gulliver/v1/models").get("downloads", []) if d.get("state") in RUNNING]
        if not busy:
            break
    final = {m["id"]: m for m in service.call("GET", "/gulliver/v1/models").get("models", [])}
    for mid in started:
        say(f"{mid} : {'installé' if final.get(mid, {}).get('installed') else 'pas installé, voir le journal du service'}")


if __name__ == "__main__":
    main()
