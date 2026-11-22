#!/usr/bin/env python3
# La page de la semaine et le conseil du modèle (fork pour macOS, 2026-11-22) : à partir des chiffres bruts de
# tools/bench_modeles.sh, le tableau de tous les modèles installés dans Gulliver, comparés au choix de la semaine
# dernière, puis le conseil (pour tout faire, le plus juste, le plus rapide, avec leurs réglages), en Markdown sur la
# sortie et, avec --json, dans conseil.json (lu par l'onglet Modèles : GET /gulliver/v1/models/advice).
#
#   python3 tools/conseil_modeles.py modeles-….raw [--previous id] [--new "id …"] [--rounds n]
#       [--machine "modèle|puce|macOS|Gulliver|charge|température"]
#       [--temperature "t_min|t_moy|t_max|c_min|c_moy|c_max|g_min|g_moy|g_max|relevés"]
#       [--late] [--json conseil.json] [--doc docs/mesures/tests-de-vitesse.md] [--service service.json] [--rag rag.json]
#
# Les lignes de llama.cpp et de MLX (identifiants « llama.cpp:… », « mlx:… ») entrent dans le tableau comme les autres,
# pas dans le conseil (le bouton « Choisir » applique un modèle de Gulliver).  Avec --doc, la section de la semaine du
# document de vitesse (entre « <!-- semaine : début --> » et « <!-- semaine : fin --> ») est réécrite : les chiffres de
# Gulliver seuls, sur les modèles mesurés, et la charge de la machine pendant la mesure.
#
# Les perplexités ne se comparent qu'entre modèles qui découpent le texte de la même façon : la famille Qwen3 (même
# vocabulaire) ; les autres ont le leur.  Le plus rapide n'est choisi que parmi les modèles d'au moins 3 milliards de
# paramètres (taille lue dans le nom) : les plus petits vont très vite mais répètent la question au lieu d'y répondre
# (Qwen2.5-0.5B, conseillé le 22 novembre 2026, l'a fait dès la première question de l'humain).
import argparse
import collections
import datetime
import json
import re

MIN_BILLIONS = 3


def fmt(v):
    if v is None:
        return "—"
    s = f"{v:,.0f}" if v >= 100 else f"{v:.1f}"
    return s.replace(",", " ").replace(".", ",")


def billions(name):
    """Le nombre de paramètres en milliards, lu dans le nom (« 0.5B », « 8B », « 30B A3B » : 30) ; None s'il n'y est pas."""
    m = re.search(r"(?<![\w.])(\d+(?:\.\d+)?)B\b", name)
    return float(m.group(1)) if m else None


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
    ap.add_argument("--service", help="le JSON de tools/bench_service.py : les mesures « comme un utilisateur »")
    ap.add_argument("--rag", help="le JSON de tools/test_rag.py : la justesse de la recherche dans les documents")
    args = ap.parse_args()
    # « t_min|t_moy|t_max|c_min|c_moy|c_max|g_min|g_moy|g_max|relevés » (bench_modeles.sh) : la température de la puce,
    # l'utilisation du processeur et du GPU pendant la mesure.
    load = args.temperature.split("|") if args.temperature else []
    num = lambda v: v.replace(".", ",")
    loads = ([("Température de la puce", "°C", load[0:3]), ("Utilisation du processeur", "%", load[3:6]),
              ("Utilisation du GPU", "%", load[6:9])] if len(load) == 10 else [])
    load_rows = [(label, f"{num(v[1])} {unit} en moyenne, de {num(v[0])} à {num(v[2])} {unit}") for label, unit, v in loads]
    temp_text = ("pendant la mesure (" + load[9] + " relevés, toutes les 20 secondes) : " + " ; ".join(
        f"{label.lower()} {num(v[1])} {unit} en moyenne (de {num(v[0])} à {num(v[2])})" for label, unit, v in loads)
        if loads else "")
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
        for label, text in load_rows:
            print(f"| {label} | {text} |")
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

    if args.service:
        try:
            service = json.load(open(args.service, encoding="utf-8"))
        except (OSError, ValueError):
            service = None
        if service:
            print()
            print(f"### Comme un utilisateur, par le service : {service.get('model', '?')}")
            print()
            print("Un service d'essai isolé (jamais celui de l'utilisateur) et llama-server (llama.cpp) sur le même fichier, "
                  "par l'interface de discussion ; les temps viennent des réponses. La conversation de 32k : un document "
                  "d'environ 32 768 tokens, une réponse de 512 au plus.")
            print()
            print("| Mesure | Gulliver | llama.cpp |")
            print("|---|---|---|")
            for label, vals in service_rows(service):
                print(f"| {label} | " + " | ".join(vals) + " |")
            for e in ("gulliver", "llama"):
                if (service.get(e) or {}).get("error"):
                    print(f"\n{'Gulliver' if e == 'gulliver' else 'llama.cpp'} : arrêté sur une erreur ({service[e]['error'][:160]}).")
    rag = load_json(args.rag)
    if rag and rag.get("results"):
        print()
        print("### La recherche dans les documents (RAG)")
        print()
        print(rag_text(rag))
        print()
        print("| Moteur | Trouvé dans les 6 premiers | Dans les 12 | Dans les 20 | Rang réciproque |")
        print("|---|---|---|---|---|")
        for row in rag_rows(rag):
            print("| " + " | ".join(row) + " |")
    if not usable:
        return
    big = [m for m in usable if (billions(names[m]) or MIN_BILLIONS) >= MIN_BILLIONS]
    fastest = max(big, key=lambda m: write[m]) if big else None
    family = [m for m in usable if comparable(names[m]) and m in ppl]
    accurate = min(family, key=lambda m: ppl[m]) if family else None
    seeing = [m for m in usable if image.get(m) and write[m] >= 40 and where.get(m) == "gpu"]
    seeing_family = [m for m in seeing if m in family]
    general = (min(seeing_family, key=lambda m: ppl[m]) if seeing_family
               else max(seeing, key=lambda m: write[m]) if seeing else fastest or max(usable, key=lambda m: write[m]))

    def entry(role, m, why):
        return {"role": role, "id": m, "name": names[m], "why": why, "write_512": write.get(m),
                "read_4096": get(m, "4096", i=0), "perplexity": ppl.get(m), "image": image.get(m, False),
                "settings": {"kv_type": kv_type(m), "draft_tokens": 7}}

    advice = [entry("general", general, "Pour tout faire, images comprises : le plus juste des modèles qui voient les "
                    "images et écrivent au moins 40 jetons/s sur le GPU.")]
    if accurate and accurate != general:
        advice.append(entry("accurate", accurate, "Les réponses les plus justes : la perplexité la plus basse de la "
                            "famille Qwen3, la seule dont les perplexités se comparent."))
    if fastest and fastest not in (general, accurate):
        advice.append(entry("fastest", fastest, "Le plus rapide : la meilleure écriture après 512 tokens, parmi les "
                            "modèles d'au moins 3 milliards de paramètres (les plus petits répètent la question au lieu "
                            "d'y répondre)."))
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
        try:
            service = json.load(open(args.service, encoding="utf-8")) if args.service else None
        except (OSError, ValueError):
            service = None
        update_doc(args.doc, [m for m in order if not engine(m) and where.get(m) != "nonlu"], names, where, get, ppl,
                   previous, advice, temp_text, args.late, service, rag)
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump({"date": datetime.datetime.now().strftime("%Y-%m-%d %H:%M"), "previous": previous or None,
                       "new": sorted(new & set(names)), "late": args.late, "advice": advice}, f, ensure_ascii=False,
                      indent=1)


def load_json(path):
    try:
        return json.load(open(path, encoding="utf-8")) if path else None
    except (OSError, ValueError):
        return None


RAG_LABELS = {"hybrid": "Hybride (mots et sens)", "dense": "Sens seul", "lexical": "Mots seuls"}


def rag_best(rag):
    """Le réglage qui trouve le plus de passages dans les 12 premiers (puis le meilleur rang) : (clé, moteur, tri fin)."""
    key = max(rag["results"], key=lambda k: (rag["results"][k]["rappel_12"], rag["results"][k]["rang_reciproque"]))
    engine, _, rerank = key.partition(" + ")
    return key, engine, bool(rerank)


def rag_rows(rag):
    """Les lignes du tableau du RAG : [moteur, trouvé à 6, 12, 20, rang réciproque]."""
    pct = lambda v: f"{round(100 * v)} %"
    best = rag_best(rag)[0]
    rows = []
    for key, r in rag["results"].items():
        engine, _, rerank = key.partition(" + ")
        label = RAG_LABELS.get(engine, engine) + (" + tri fin" if rerank else "") + (" (le meilleur)" if key == best else "")
        rows.append([label, pct(r["rappel_6"]), pct(r["rappel_12"]), pct(r["rappel_20"]),
                     f"{r['rang_reciproque']:.2f}".replace(".", ",")])
    return rows


def rag_text(rag):
    _, engine, rerank = rag_best(rag)
    return (f"{rag.get('questions', '?')} questions figées sur les {rag.get('documents', '?')} épisodes du blog, cherchées "
            f"par un service d'essai isolé ({rag.get('embedder', '?')}, épisodes indexés en "
            f"{round(rag.get('index_s', 0))} s) ; une question est trouvée quand le passage qui y répond est rendu. Le "
            f"meilleur réglage : moteur « {RAG_LABELS.get(engine, engine)} », tri fin "
            f"{'allumé' if rerank else 'éteint'}, 12 passages.")


def service_rows(service, engines=("gulliver", "llama")):
    """Les lignes du tableau « comme un utilisateur » (tools/bench_service.py) : [(libellé, [valeur par moteur])]."""
    def f(v, unit="", digits=None):
        if v is None:
            return "—"
        return (fmt(v) if digits is None else f"{v:.{digits}f}".replace(".", ",")) + unit
    def get(e, *keys):
        d = service.get(e) or {}
        for k in keys:
            d = d.get(k) if isinstance(d, dict) else None
        return d
    rows = []
    for label, keys, unit, digits in (
            ("Chargement du modèle (jusqu'à une première réponse)", ("load_s",), " s", 1),
            ("Conversation de 32k : lecture", ("long_drafts", "read"), " jetons/s", None),
            ("Conversation de 32k : écriture, avec brouillons", ("long_drafts", "write"), " jetons/s", None),
            ("Conversation de 32k : écriture, sans brouillons", ("long_plain", "write"), " jetons/s", None),
            ("4 requêtes à la fois : écriture totale", ("four", "total_write"), " jetons/s", None),
            ("3 documents de 4 000 tokens à la fois : temps total", ("three_docs", "total_s"), " s", 1),
            ("3 documents à la fois : attente moyenne du premier mot", ("three_docs", "mean_first_token_s"), " s", 1)):
        vals = []
        for e in engines:
            v = get(e, *keys)
            if e == "llama" and keys[0] == "long_drafts" and keys[1] == "read":
                v = get(e, "long_plain", "read")
            vals.append(f(v, unit, digits))
        rows.append((label, vals))
    big = []
    for e in engines:
        b = get(e, "largest")
        text = f"{b['context']:,}".replace(",", " ") + " tokens" if isinstance(b, dict) and b.get("context") else "—"
        if isinstance(b, dict) and b.get("refused"):
            text += f" ({b['refused']:,} refusés)".replace(",", " ")
        big.append(text)
    rows.append(("Le plus grand contexte qui tient (essayés : 65 536 et 131 072)", big))
    return rows


def update_doc(path, models, names, where, get, ppl, previous, advice, temp_text, late, service=None, rag=None):
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
             + (f" Charge de la machine {temp_text}." if temp_text else "")
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
    if service and service.get("gulliver"):
        lines += ["", f"Comme un utilisateur, par le service, sur {service.get('model', '?')} :", "", "| Mesure | Gulliver |",
                  "|---|---|"]
        lines += [f"| {label} | {vals[0]} |" for label, vals in service_rows(service, ("gulliver",))]
    if rag and rag.get("results"):
        lines += ["", "La recherche dans les documents (RAG) : " + rag_text(rag), "",
                  "| Moteur | Trouvé dans les 6 premiers | Dans les 12 | Dans les 20 | Rang réciproque |", "|---|---|---|---|---|"]
        lines += ["| " + " | ".join(row) + " |" for row in rag_rows(rag)]
    lines += ["", end]
    a, b = text.index(begin), text.index(end) + len(end)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text[:a] + "\n".join(lines) + text[b:])


if __name__ == "__main__":
    main()
