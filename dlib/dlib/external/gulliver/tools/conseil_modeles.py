#!/usr/bin/env python3
# Le conseil de la semaine (fork pour macOS, 2026-11-22) : quel modèle employer avec Gulliver, et avec quels réglages,
# tiré des tableaux d'une mesure du dimanche (bench_moteurs.sh puis justesse_moteurs.sh, tels que bench_hebdo.sh les
# assemble).  Écrit la section « Conseil » en Markdown sur la sortie ; avec --ids, les modèles installés dans Gulliver
# (le fichier .ids de bench_moteurs.sh : nom|identifiant|actif|images) ; avec --json, le conseil que lit l'onglet
# Modèles (GET /gulliver/v1/models/advice).
#
#   python3 tools/conseil_modeles.py semaine.md [--ids moteurs-….ids] [--json conseil.json]
#
# Seules les colonnes de Gulliver comptent (vitesse sur le GPU, perplexité sur le GPU).  Les perplexités ne se
# comparent qu'entre modèles qui découpent le texte de la même façon : la famille Qwen3 (même vocabulaire) ; Gemma,
# LFM2, Mistral et les autres ont le leur.
import argparse
import datetime
import json
import re


def number(cell):
    """Le premier nombre d'une case (« 2 679 », « 82,0 (+4 %) ») ou None."""
    m = re.match(r"\s*([\d   ]+(?:,\d+)?)", cell)
    if not m:
        return None
    try:
        return float(re.sub(r"[\s  ]", "", m.group(1)).replace(",", "."))
    except ValueError:
        return None


def percents(cell):
    """Les écarts en % d'une case des autres réglages (« 2 205 (±0 %) / 71,7 (+1 %) ») : (lecture, écriture)."""
    found = re.findall(r"\(([±+−-])(\d+) %\)", cell)
    vals = [(-1 if s in "−-" else 1) * int(v) for s, v in found]
    return (vals[0], vals[1]) if len(vals) == 2 else (None, None)


def tables(text):
    """Les tableaux du texte, chacun avec le titre qui le précède : [(titre, [lignes de cases])]."""
    out, title, rows = [], "", None
    for line in text.splitlines():
        if line.startswith("#"):
            title = line.lstrip("#").strip()
        if line.startswith("|"):
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if rows is None:
                rows = []
                out.append((title, rows))
            if not all(re.fullmatch(r"-+", c) for c in cells):
                rows.append(cells)
        else:
            rows = None
    return out


def fmt(v):
    if v is None:
        return "?"
    s = f"{v:,.0f}" if v >= 1000 else (f"{v:.0f}" if v >= 100 else f"{v:.1f}")
    return s.replace(",", " ").replace(".", ",")


def comparable(name):
    """Les modèles dont la perplexité se compare : la famille Qwen3 (pas Qwen3.5, qui a son propre vocabulaire)."""
    return name.startswith("Qwen3-") or name.startswith("Qwen3 ")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("week")
    ap.add_argument("--ids")
    ap.add_argument("--json")
    args = ap.parse_args()
    text = open(args.week, encoding="utf-8").read()
    read, write, ppl, f16 = {}, {}, {}, {}
    for title, rows in tables(text):
        if not rows:
            continue
        head = rows[0]
        if title.startswith("Lecture") and head[:3] == ["Modèle", "Question", "Gulliver"]:
            for r in rows[1:]:
                read[(r[0], r[1])] = number(r[2])
        elif title.startswith("Écriture") and head[:3] == ["Modèle", "Question", "Gulliver"]:
            for r in rows[1:]:
                write[(r[0], r[1])] = number(r[2])
        elif title.startswith("Les autres réglages") and head[:3] == ["Modèle", "Question", "Gulliver, clés en demi-précision"]:
            for r in rows[1:]:
                f16.setdefault(r[0], []).append(percents(r[2]))
        elif head[:2] == ["Modèle", "Gulliver, GPU"]:
            for r in rows[1:]:
                if "référence" not in r[0]:
                    ppl[r[0]] = number(r[1])
    models = [m for m in dict.fromkeys(k[0] for k in write) if write.get((m, "512"))]
    if not models:
        return
    # Les modèles installés dans Gulliver : seuls ceux-là peuvent être choisis depuis l'onglet Modèles.
    ids, active, image = {}, None, set()
    if args.ids:
        try:
            for line in open(args.ids, encoding="utf-8"):
                parts = line.rstrip("\n").split("|")
                if len(parts) >= 3 and parts[0] in models:
                    ids[parts[0]] = parts[1]
                    if parts[2] == "1":
                        active = parts[0]
                    if len(parts) >= 4 and parts[3] == "1":
                        image.add(parts[0])
        except OSError:
            pass
    pool = [m for m in models if m in ids] or models

    def kv_type(m):
        """La mémoire de conversation : demi-précision seulement si elle écrit au moins 3 % plus vite en moyenne,
        sans lire plus lentement ; sinon 8 bits, deux fois moins de mémoire pour les mêmes réponses."""
        vals = [v for v in f16.get(m, []) if v[0] is not None]
        if not vals:
            return "q8"
        w = sum(v[1] for v in vals) / len(vals)
        r = sum(v[0] for v in vals) / len(vals)
        return "f16" if w >= 3 and r >= 0 else "q8"

    fastest = max(pool, key=lambda m: write[(m, "512")])
    family = [m for m in pool if comparable(m) and ppl.get(m)]
    accurate = min(family, key=lambda m: ppl[m]) if family else None
    seeing = [m for m in pool if m in image and write[(m, "512")] >= 40]
    seeing_family = [m for m in seeing if m in family]
    general = (min(seeing_family, key=lambda m: ppl[m]) if seeing_family
               else max(seeing, key=lambda m: write[(m, "512")]) if seeing else fastest)

    def entry(role, m, why):
        return {"role": role, "id": ids.get(m), "name": m, "why": why,
                "write_512": write.get((m, "512")), "read_4096": read.get((m, "4096")), "perplexity": ppl.get(m),
                "image": m in image, "settings": {"kv_type": kv_type(m), "draft_tokens": 7}}

    advice = [entry("general", general, "Pour tout faire, images comprises : le plus juste des modèles qui voient les "
                    "images et écrivent au moins 40 jetons/s.")]
    if accurate and accurate != general:
        advice.append(entry("accurate", accurate, "Les réponses les plus justes : la perplexité la plus basse de la "
                            "famille Qwen3, la seule dont les perplexités se comparent."))
    if fastest not in (general, accurate):
        advice.append(entry("fastest", fastest, "Le plus rapide : la meilleure écriture après 512 tokens."))

    def line(m):
        moe = " ; modèle à experts : tout le fichier reste en mémoire" if "MoE" in m else ""
        p = ", perplexité " + f"{ppl[m]:.2f}".replace(".", ",") if ppl.get(m) else ""
        state = " ; c'est le modèle actif" if m == active else ""
        return (f"**{m}** : écriture {fmt(write.get((m, '512')))} jetons/s après 512 tokens, "
                f"lecture {fmt(read.get((m, '4096')))} jetons/s d'une question de 4 096 tokens{p}{moe}{state} ; "
                f"mémoire de conversation conseillée : {'demi-précision' if kv_type(m) == 'f16' else '8 bits'}")

    labels = {"general": "Pour tout faire, images comprises", "accurate": "Pour les réponses les plus justes",
              "fastest": "Pour aller vite"}
    print("### Conseil : quel modèle employer avec Gulliver")
    print()
    print("Tiré des tableaux de cette semaine (colonnes de Gulliver, sur le GPU)"
          + (", parmi les modèles installés dans Gulliver ; le bouton « Choisir » de l'onglet Modèles applique le "
             "modèle et ses réglages" if ids else "") + ".")
    print()
    for a in advice:
        print(f"- **{labels[a['role']]}** : {line(a['name'])}.")
    if active and active not in [a["name"] for a in advice]:
        print(f"- Le modèle actif aujourd'hui : {line(active)}.")
    print()
    print("À lire avec deux précautions : la perplexité mesure la façon dont un modèle prévoit un texte français "
          "(dix épisodes du blog), pas la qualité de ses raisonnements ; et elle ne se compare qu'entre modèles de "
          "la même famille (les Qwen3 entre eux). L'écriture est mesurée sans brouillon : sur une recopie ou une "
          "question sur un document, les brouillons la multiplient par 1,3 à 3.")
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump({"date": datetime.datetime.now().strftime("%Y-%m-%d %H:%M"), "active": ids.get(active),
                       "advice": advice}, f, ensure_ascii=False, indent=1)


if __name__ == "__main__":
    main()
