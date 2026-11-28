#!/usr/bin/env python3
# Copyright (C) 2026  papy cyber
# License: Boost Software License   See LICENSE.txt for the full license.
"""Le retrait du dernier du classement, après la mesure du lundi (fork pour macOS, 2026-11-24, décision de l'humain :
« tu le retires seul », et un menu des modèles retirés avec leur date).

    python3 tools/retrait_modeles.py GULLIVER_HOME [--essai]

conseil.json (programme conseil, bench/conseil_main.cpp) désigne dans « remove » le dernier du classement : la plus basse note à
l'épreuve, puis la plus lente écriture, hors des modèles conseillés, du choix précédent et des modèles actifs.  Ce
programme le retire de Gulliver par le service qui tourne sur ce Mac (DELETE /gulliver/v1/models/<id> : le fichier est
effacé), l'ajoute à build/bench/hebdo/retires.json avec sa date, sa note et de quoi le remettre (dépôt, fichier et
projecteur de Hugging Face, ou identifiant du catalogue), et recopie la liste dans « removed » de conseil.json : le
menu « Modèles retirés » de l'onglet Modèles la montre, avec un bouton pour le remettre.  La veille du lundi
(tools/modeles_semaine.py) ne retélécharge jamais un modèle de cette liste.  Écrit une ligne « retiré : … » pour le
mail de la mesure ; rien sans « remove », et un seul retrait par semaine.  Avec --essai, il dit seulement ce qu'il ferait.
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from modeles_semaine import Service  # noqa: E402

HEBDO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../../../../build/bench/hebdo"))


def load(path, default):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return default


def write(path, value):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(value, f, ensure_ascii=False, indent=1)
    os.replace(tmp, path)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    trial = "--essai" in sys.argv
    home = args[0] if args else os.environ.get("GULLIVER_HOME", "")
    hebdo = os.environ.get("RETRAIT_HEBDO") or HEBDO   # un autre dossier, pour le test
    advice_path, removed_path = os.path.join(hebdo, "conseil.json"), os.path.join(hebdo, "retires.json")
    advice = load(advice_path, {})
    target = advice.get("remove") or {}
    removed = load(removed_path, [])
    mid = target.get("id")
    if not mid:
        print("rien à retirer cette semaine")
        return 0
    # Un seul retrait par semaine : une mesure relancée dans la semaine ne retire pas un deuxième modèle.
    recent = [x for x in removed if time.time() - time.mktime(time.strptime(x.get("date", "1970-01-01 00:00"),
                                                                           "%Y-%m-%d %H:%M")) < 6 * 86400]
    if recent:
        print(f"déjà un retrait cette semaine ({recent[-1]['name']}, le {recent[-1]['date']}) : rien de plus")
        advice["removed"] = removed
        write(advice_path, advice)
        return 0
    service = Service(home)
    data = service.call("GET", "/gulliver/v1/models")
    model = next((m for m in data.get("models", []) if m.get("id") == mid), None)
    if not model or not model.get("installed"):
        print(f"{mid} : déjà absent de Gulliver")
        return 0
    if mid in set((data.get("active") or {}).values()):
        print(f"{mid} : actif, laissé")
        return 0
    entry = {"date": time.strftime("%Y-%m-%d %H:%M"), "id": mid, "name": target.get("name") or model.get("name") or mid,
             "origin": model.get("origin") or "catalog", "quiz": target.get("quiz"), "write_512": target.get("write_512"),
             "why": "le moteur ne le lit pas" if target.get("unread") else "le dernier du classement"}
    if entry["origin"] == "huggingface":
        url = (model.get("files") or [{}])[0].get("url", "")
        entry["repo"] = (model.get("source") or "").replace("https://huggingface.co/", "")
        entry["file"] = url.split("/resolve/", 1)[1].split("/", 1)[1] if "/resolve/" in url else os.path.basename(
            model.get("file") or "")
        entry["projector"] = (model.get("projector") or {}).get("name", "")
    note = f"épreuve {entry['quiz']:.0f}/100" if entry.get("quiz") is not None else entry["why"]
    if trial:
        print(f"essai : retirerait {entry['name']} ({note})")
        return 0
    r = service.call("DELETE", f"/gulliver/v1/models/{mid}")
    if not r.get("ok"):
        print(f"{mid} : pas retiré ({r.get('code') or r.get('error')})")
        return 1
    removed = [x for x in removed if x.get("id") != mid] + [entry]
    write(removed_path, removed)
    advice["removed"] = removed
    write(advice_path, advice)
    print(f"retiré : {entry['name']} ({note}) ; la veille ne le retéléchargera plus, le menu « Modèles retirés » de "
          f"l'onglet Modèles le remet d'un clic")
    return 0


if __name__ == "__main__":
    sys.exit(main())
