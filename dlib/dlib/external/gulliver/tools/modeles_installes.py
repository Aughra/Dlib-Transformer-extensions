#!/usr/bin/env python3
# Les modèles installés dans Gulliver, pour les mesures du dimanche (fork pour macOS, 2026-11-22).
#
#   python3 tools/modeles_installes.py [GULLIVER_HOME]
#
# Demande au service qui tourne sur ce Mac (https://127.0.0.1:<port de ses réglages>, certificat du service
# accepté sans vérification : c'est le même Mac) la liste de ses modèles, et écrit une ligne par modèle installé
# qui répond aux questions (les modèles de la recherche dans les documents, rôle « embedding », sont écartés) :
#
#   nom affiché|fichier GGUF|identifiant|actif (1 ou 0)|lit les images (1 ou 0)
#
# Rien si le service ne répond pas : les mesures se font alors sur leur liste fixe seulement.
import json
import os
import ssl
import sys
import urllib.request


def main():
    home = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("GULLIVER_HOME", "")
    port = 18443
    try:
        with open(os.path.join(home, "settings.json"), encoding="utf-8") as f:
            port = int(json.load(f).get("port", port))
    except (OSError, ValueError):
        pass
    context = ssl.create_default_context()
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE
    try:
        with urllib.request.urlopen(f"https://127.0.0.1:{port}/gulliver/v1/models", context=context, timeout=10) as r:
            data = json.load(r)
    except Exception:
        return
    active = set((data.get("active") or {}).values())
    for m in data.get("models", []):
        roles = m.get("roles") or []
        if not m.get("installed") or not m.get("file") or "embedding" in roles or not os.path.isfile(m["file"]):
            continue
        name = m.get("name") or m["id"]
        if m.get("quantization") and m["quantization"] not in name:
            name += " " + m["quantization"]
        if "moe" in (m.get("arch") or ""):
            name += " (MoE)"
        name = name.replace("|", "/")
        image = 1 if "image" in (m.get("inputs") or []) else 0
        print(f"{name}|{m['file']}|{m['id']}|{1 if m['id'] in active else 0}|{image}")


if __name__ == "__main__":
    main()
