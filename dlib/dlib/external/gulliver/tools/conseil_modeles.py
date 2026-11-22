#!/usr/bin/env python3
# La page de la semaine et le conseil du modèle (fork pour macOS, 2026-11-22) : à partir des chiffres bruts de
# tools/bench_modeles.sh, le tableau de tous les modèles installés dans Gulliver, comparés au choix de la semaine
# dernière, puis le conseil (pour tout faire, le plus juste, le plus rapide, avec leurs réglages), en Markdown sur la
# sortie et, avec --json, dans conseil.json (lu par l'onglet Modèles : GET /gulliver/v1/models/advice).
#
#   python3 tools/conseil_modeles.py modeles-….raw [--previous id] [--new "id …"] [--rounds n]
#       [--machine "modèle|puce|macOS|Gulliver|charge|température"] [--temperature "moyenne|maximum|relevés"]
#       [--late] [--json conseil.json] [--doc docs/mesures/tests-de-vitesse.md]
#
# Les lignes de llama.cpp et de MLX (identifiants « llama.cpp:… », « mlx:… ») entrent dans le tableau comme les autres,
# pas dans le conseil (le bouton « Choisir » applique un modèle de Gulliver).  Avec --doc, la section de la semaine du
# document de vitesse (entre « <!-- semaine : début --> » et « <!-- semaine : fin --> ») est réécrite : les chiffres de
# Gulliver seuls, sur les modèles mesurés, et la température moyenne de la puce pendant la mesure.
#
# Les perplexités ne se comparent qu'entre modèles qui découpent le texte de la même façon : la famille Qwen3 (même
# vocabulaire) ; les autres ont le leur.
import argparse
import collections
import datetime
import json


def fmt(v):
    if v is None:
        return "—"
    s = f"{v:,.0f}" if v >= 100 else f"{v:.1f}"
    return s.replace(",", " ").replace(".", ",")


def comparable(name):
    """La famille Qwen3 (pas Qwen3.5, qui a son propre vocabulaire)."""
    return name.startswith("Qwen3-") or name.startswith("Qwen3 ")


def mean(values):
    nums = []
    for v in values:
        try:
            nums.append(float(v))
        except ValueError:
            pass
    return sum(nums) / len(nums) if nums else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("raw")
    ap.add_argument("--previous", default="")
    ap.add_argument("--new", default="")
    ap.add_argument("--rounds", default="2")
    ap.add_argument("--machine", default="")
    ap.add_argument("--late", action="store_true")
    ap.add_argument("--json")
    ap.add_argument("--temperature", default="")
    ap.add_argument("--doc")
    args = ap.parse_args()
    temp = args.temperature.split("|") if args.temperature else []
    temp_text = (f"{temp[0].replace('.', ',')} °C en moyenne pendant la mesure, {temp[1].replace('.', ',')} °C au plus "
                 f"({temp[2]} relevés)") if len(temp) == 3 else ""
    new = set(args.new.split())

    names, image, where, ppl = {}, {}, {}, {}
    runs = collections.defaultdict(lambda: collections.defaultdict(list))   # (id, taille) -> mémoire -> [(lecture, écriture)]
    for line in open(args.raw, encoding="utf-8"):
        p = line.rstrip("\n").split("|")
        if p[0] == "ppl" and len(p) == 3:
            try:
                ppl[p[1]] = float(p[2])
            except ValueError:
                pass
            continue
        if len(p) != 8:
            continue
        mid, name, img, n, kv, r, w, calc = p
        names[mid], image[mid] = name, img == "1"
        # Le pire de ses essais : ne lit pas, puis processeur, puis GPU.
        rank = {"nonlu": 2, "processeur": 1, "gpu": 0}
        if rank.get(calc, 0) >= rank.get(where.get(mid, "gpu"), 0):
            where[mid] = calc
        runs[(mid, n)][kv].append((r, w))
    models = list(names)
    if not models:
        return

    def get(mid, n, kv="q8", i=1):
        return mean(v[i] for v in runs[(mid, n)].get(kv, []))

    def kv_type(mid):
        """Demi-précision seulement si elle écrit au moins 3 % plus vite en moyenne sans lire plus lentement ; sinon
        8 bits, deux fois moins de mémoire pour les mêmes réponses."""
        dw, dr = [], []
        for n in ("512", "4096"):
            a, b = get(mid, n, "q8"), get(mid, n, "f16")
            ra, rb = get(mid, n, "q8", 0), get(mid, n, "f16", 0)
            if a and b:
                dw.append(b / a - 1)
            if ra and rb:
                dr.append(rb / ra - 1)
        return "f16" if dw and sum(dw) / len(dw) >= 0.03 and (not dr or sum(dr) / len(dr) >= 0) else "q8"

    engine = lambda m: ":" in m   # llama.cpp et MLX : comparés, jamais conseillés
    usable = [m for m in models if where.get(m) != "nonlu" and get(m, "512") and not engine(m)]
    write = {m: get(m, "512") for m in models if get(m, "512")}
    previous = args.previous if args.previous in names else ""
    base = write.get(previous)

    print(f"### Les modèles de Gulliver — {datetime.datetime.now().strftime('%Y-%m-%d %H:%M')}")
    print()
    if args.machine:
        labels = ["Modèle de Mac", "Puce", "macOS", "Gulliver", "Charge", "Puce, sonde la plus chaude"]
        print("| Machine | |")
        print("|---|---|")
        for label, value in zip(labels, args.machine.split("|")):
            print(f"| {label} | {value} |")
        if temp_text:
            print(f"| Température de la puce | {temp_text} |")
        print()
    print(f"Gulliver sur chacun de ses modèles installés, et llama.cpp et MLX sur le modèle de référence : lecture d'une question puis écriture de 64 jetons après elle, en jetons par "
          f"seconde, mémoire de conversation en 8 bits, {args.rounds} essais en alternance (moyenne) ; perplexité d'un "
          f"même texte français (plus basse, mieux le modèle prévoit le texte). "
          + (f"Référence : **{names[previous]}**, le choix de la semaine dernière ; l'écart est celui de l'écriture "
             f"après 512 tokens (+ : plus rapide que lui)." if previous else
             "Pas encore de choix de la semaine dernière pour référence."))
    if new & set(names):
        print()
        print("Nouveaux cette semaine (téléchargés la nuit précédente) : " + ", ".join(names[m] for m in names if m in new) + ".")
    if args.late:
        print()
        print("L'heure limite est passée avant la fin : les modèles manquants n'ont pas été mesurés cette semaine.")
    print()
    print("| Modèle | Calcul | Lecture 512 | Lecture 4 096 | Écriture 512 | Écriture 4 096 | Écart | Perplexité "
          "| Mémoire conseillée |")
    print("|---|---|---|---|---|---|---|---|---|")
    order = sorted(models, key=lambda m: -(write.get(m) or 0))
    for m in order:
        tag = " (nouveau)" if m in new else ""
        tag += " (choix précédent)" if m == previous else ""
        if where.get(m) == "nonlu":
            print(f"| {names[m]}{tag} | ne lit pas ce modèle | — | — | — | — | — | — | — |")
            continue
        d = ""
        if base and write.get(m) and m != previous:
            pct = round(100 * (write[m] / base - 1))
            d = f"{'±' if pct == 0 else '+' if pct > 0 else '−'}{abs(pct)} %"
        p = f"{ppl[m]:.2f}".replace(".", ",") if m in ppl else "—"
        memory = "—" if engine(m) else ("demi-précision" if kv_type(m) == "f16" else "8 bits")
        print(f"| {names[m]}{tag} | {'GPU' if where.get(m) == 'gpu' else 'processeur'} | {fmt(get(m, '512', i=0))} "
              f"| {fmt(get(m, '4096', i=0))} | {fmt(get(m, '512'))} | {fmt(get(m, '4096'))} | {d or '—'} | {p} "
              f"| {memory} |")

    if not usable:
        return
    fastest = max(usable, key=lambda m: write[m])
    family = [m for m in usable if comparable(names[m]) and m in ppl]
    accurate = min(family, key=lambda m: ppl[m]) if family else None
    seeing = [m for m in usable if image.get(m) and write[m] >= 40 and where.get(m) == "gpu"]
    seeing_family = [m for m in seeing if m in family]
    general = (min(seeing_family, key=lambda m: ppl[m]) if seeing_family
               else max(seeing, key=lambda m: write[m]) if seeing else fastest)

    def entry(role, m, why):
        return {"role": role, "id": m, "name": names[m], "why": why, "write_512": write.get(m),
                "read_4096": get(m, "4096", i=0), "perplexity": ppl.get(m), "image": image.get(m, False),
                "settings": {"kv_type": kv_type(m), "draft_tokens": 7}}

    advice = [entry("general", general, "Pour tout faire, images comprises : le plus juste des modèles qui voient les "
                    "images et écrivent au moins 40 jetons/s sur le GPU.")]
    if accurate and accurate != general:
        advice.append(entry("accurate", accurate, "Les réponses les plus justes : la perplexité la plus basse de la "
                            "famille Qwen3, la seule dont les perplexités se comparent."))
    if fastest not in (general, accurate):
        advice.append(entry("fastest", fastest, "Le plus rapide : la meilleure écriture après 512 tokens."))
    labels = {"general": "Pour tout faire, images comprises", "accurate": "Pour les réponses les plus justes",
              "fastest": "Pour aller vite"}
    print()
    print("### Conseil : quel modèle employer avec Gulliver")
    print()
    for a in advice:
        m = a["id"]
        p = ", perplexité " + f"{ppl[m]:.2f}".replace(".", ",") if m in ppl else ""
        print(f"- **{labels[a['role']]}** : **{names[m]}**, écriture {fmt(write[m])} jetons/s après 512 tokens{p} ; "
              f"mémoire de conversation conseillée : {'demi-précision' if a['settings']['kv_type'] == 'f16' else '8 bits'}.")
    if previous:
        print()
        print("Par rapport à la semaine dernière : " + ("le conseil pour tout faire ne change pas."
              if general == previous else f"le conseil pour tout faire passe de {names[previous]} à {names[general]}."))
    print()
    print("Le bouton « Choisir » de l'onglet Modèles de Gulliver applique le modèle et ses réglages. À lire avec deux "
          "précautions : la perplexité mesure la façon dont un modèle prévoit un texte français, pas la qualité de ses "
          "raisonnements, et elle ne se compare qu'entre modèles Qwen3 ; l'écriture est mesurée sans brouillon (sur une "
          "recopie ou une question sur un document, les brouillons la multiplient par 1,3 à 3).")
    if args.doc:
        update_doc(args.doc, [m for m in order if not engine(m) and where.get(m) != "nonlu"], names, where, get, ppl,
                   previous, advice, temp_text, args.late)
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump({"date": datetime.datetime.now().strftime("%Y-%m-%d %H:%M"), "previous": previous or None,
                       "new": sorted(new & set(names)), "late": args.late, "advice": advice}, f, ensure_ascii=False,
                      indent=1)


def update_doc(path, models, names, where, get, ppl, previous, advice, temp_text, late):
    """La section de la semaine du document de vitesse : Gulliver seul, sur les modèles mesurés cette semaine."""
    begin, end = "<!-- semaine : début -->", "<!-- semaine : fin -->"
    try:
        text = open(path, encoding="utf-8").read()
    except OSError:
        return
    if begin not in text or end not in text:
        return
    when = datetime.datetime.now().strftime("%d/%m/%Y à %H h %M")
    lines = [begin, "", f"## La mesure de la semaine ({when})", "",
             "Écrite par le Mac à chaque mesure de la semaine, le lundi (`tools/bench_modeles.sh`) : Gulliver seul, "
             "sur chacun de ses modèles installés ; jetons par seconde, mémoire de conversation en 8 bits."
             + (f" Température de la puce : {temp_text}." if temp_text else "")
             + (" L'heure limite a coupé la mesure avant la fin." if late else ""), "",
             "| Modèle | Calcul | Lecture 512 | Lecture 4 096 | Écriture 512 | Écriture 4 096 | Perplexité |",
             "|---|---|---|---|---|---|---|"]
    for m in models:
        tag = " (choix précédent)" if m == previous else ""
        p = f"{ppl[m]:.2f}".replace(".", ",") if m in ppl else "—"
        lines.append(f"| {names[m]}{tag} | {'GPU' if where.get(m) == 'gpu' else 'processeur'} | {fmt(get(m, '512', i=0))} "
                     f"| {fmt(get(m, '4096', i=0))} | {fmt(get(m, '512'))} | {fmt(get(m, '4096'))} | {p} |")
    if advice:
        lines += ["", "Conseillé : " + " ; ".join(
            {"general": "pour tout faire", "accurate": "le plus juste", "fastest": "le plus rapide"}[a["role"]] + ", "
            + names[a["id"]] for a in advice) + ". Les tableaux complets, avec llama.cpp et MLX, sont dans « Mesures de "
                                                    "la semaine »."]
    lines += ["", end]
    a, b = text.index(begin), text.index(end) + len(end)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text[:a] + "\n".join(lines) + text[b:])


if __name__ == "__main__":
    main()
