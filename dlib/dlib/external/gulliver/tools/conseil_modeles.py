#!/usr/bin/env python3
# Le conseil de la semaine (fork pour macOS, 2026-11-22) : quel modèle employer avec Gulliver, tiré des
# tableaux d'une mesure du dimanche (bench_moteurs.sh puis justesse_moteurs.sh, tels que bench_hebdo.sh
# les assemble).  Lit le fichier de la semaine, écrit la section « Conseil » en Markdown sur la sortie.
#
#   python3 tools/conseil_modeles.py build/bench/hebdo/2026-11-22.md
#
# Seules les colonnes de Gulliver comptent (vitesse sur le GPU, perplexité sur le GPU).  Les perplexités ne
# se comparent qu'entre modèles qui découpent le texte de la même façon : la famille Qwen3 (même
# vocabulaire) ; Gemma et LFM2 ont le leur.
import re
import sys


def number(cell):
    """Le premier nombre d'une case (« 2 679 », « 82,0 (+4 %) ») ou None."""
    m = re.match(r"\s*([\d   ]+(?:,\d+)?)", cell)
    if not m:
        return None
    try:
        return float(m.group(1).replace(" ", "").replace(" ", "").replace(" ", "").replace(",", "."))
    except ValueError:
        return None


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


def main():
    text = open(sys.argv[1], encoding="utf-8").read()
    read, write, ppl = {}, {}, {}
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
        elif head[:2] == ["Modèle", "Gulliver, GPU"]:
            for r in rows[1:]:
                if "référence" not in r[0]:
                    ppl[r[0]] = number(r[1])
    models = [m for m in dict.fromkeys(k[0] for k in write) if (m, "512") in write]
    if not models:
        return
    service = next((m for m in models if "service" in m), models[0])
    qwen = [m for m in models if m.startswith("Qwen3") and ppl.get(m)]
    best = min(qwen, key=lambda m: ppl[m]) if qwen else None
    fastest = max(models, key=lambda m: write[(m, "512")])

    def line(m):
        moe = " ; modèle à experts : tout le fichier reste en mémoire, il lui faut plus de mémoire qu'aux autres" if "MoE" in m else ""
        p = ", perplexité " + f"{ppl[m]:.2f}".replace(".", ",") if ppl.get(m) else ""
        return (f"**{m}** : écriture {fmt(write.get((m, '512')))} jetons/s après 512 tokens, "
                f"lecture {fmt(read.get((m, '4096')))} jetons/s d'une question de 4 096 tokens{p}{moe}")

    print("### Conseil : quel modèle employer avec Gulliver")
    print()
    print("Tiré des tableaux de cette semaine (colonnes de Gulliver, sur le GPU).")
    print()
    print(f"- **Pour tout faire, images comprises** : le modèle du service, {line(service)}.")
    if best and best != service:
        gain = (ppl[service] - ppl[best]) / ppl[service] * 100 if ppl.get(service) else None
        slower = (1 - write[(best, '512')] / write[(service, '512')]) * 100
        print(f"- **Pour les réponses les plus justes (texte seul)** : {line(best)}. Sa perplexité est "
              f"{fmt(gain)} % plus basse que celle du modèle du service, pour une écriture "
              f"{'plus lente de ' + fmt(slower) + ' %' if slower > 0 else 'aussi rapide'}"
              + (" ; sa lecture des longues questions est plus lente" if read.get((best, '4096'), 0) < read.get((service, '4096'), 0) else "")
              + ".")
    if fastest not in (service, best):
        print(f"- **Pour aller vite (questions courtes, tâches simples)** : {line(fastest)}. "
              f"Plus petit, il se trompe davantage sur les questions difficiles.")
    print()
    print("À lire avec deux précautions : la perplexité mesure la façon dont un modèle prévoit un texte français "
          "(dix épisodes du blog), pas la qualité de ses raisonnements ; et elle ne se compare qu'entre modèles de "
          "la même famille (les Qwen3 entre eux), Gemma et LFM2 découpant le texte autrement. L'écriture est mesurée "
          "sans brouillon : sur une recopie ou une question sur un document, les brouillons la multiplient par 1,3 à 3.")


if __name__ == "__main__":
    main()
