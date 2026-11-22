#!/usr/bin/env python3
# Le test du conseil de la semaine (fork pour macOS, 2026-11-22) : tools/conseil_modeles.py sur des chiffres bruts figés,
# dont on connaît les bonnes réponses.
#
#   python3 tools/test_conseil.py
#
# Vérifie : le modèle pour tout faire (le plus juste des Qwen3 qui voient les images et écrivent au moins 40 jetons/s
# sur le GPU), le plus juste, le plus rapide ; que llama.cpp et MLX sont dans le tableau mais jamais conseillés ; la
# règle de la mémoire de conversation (demi-précision seulement si elle écrit au moins 3 % plus vite) ; la charge de la
# machine ; et la réécriture de la section de la semaine du document entre ses marqueurs, le reste du document intact.
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
failures = 0


def check(ok, what):
    global failures
    print(("ok   " if ok else "FAIL ") + what)
    failures += 0 if ok else 1


def rows(mid, name, image, calc, values):
    """values : {(taille, mémoire): (lecture, écriture)}, deux essais identiques."""
    out = []
    for (n, kv), (r, w) in values.items():
        out += [f"{mid}|{name}|{image}|{n}|{kv}|{r}|{w}|{calc}"] * 2
    return out


raw = []
# A : Qwen3 qui voit les images, rapide, le moins juste des Qwen3 ; la demi-précision ne lui gagne que 1 %.
raw += rows("a", "Qwen3-VL 4B A", 1, "gpu", {("512", "q8"): (2600, 88), ("512", "f16"): (2600, 89),
                                          ("4096", "q8"): (2200, 74), ("4096", "f16"): (2200, 74.5)})
# B : Qwen3 qui voit les images, un peu plus lent mais plus juste ; la demi-précision lui gagne 5 % : f16 conseillée.
raw += rows("b", "Qwen3-VL 30B B (MoE)", 1, "gpu", {("512", "q8"): (1200, 80), ("512", "f16"): (1210, 84),
                                                 ("4096", "q8"): (1400, 70), ("4096", "f16"): (1410, 73.5)})
# C : le plus rapide de tous, autre famille (sa perplexité ne se compare pas), sur le processeur.
raw += rows("c", "Petit C", 0, "processeur", {("512", "q8"): (1500, 190), ("512", "f16"): (1500, 185),
                                             ("4096", "q8"): (1400, 170), ("4096", "f16"): (1400, 168)})
# D : Qwen3 très juste mais trop lent pour « tout faire » (sous 40 jetons/s) ; il reste le plus juste.
raw += rows("d", "Qwen3 D", 1, "gpu", {("512", "q8"): (300, 30), ("512", "f16"): (300, 30),
                                     ("4096", "q8"): (250, 25), ("4096", "f16"): (250, 25)})
# llama.cpp, plus rapide que tous : dans le tableau, jamais conseillé.
raw += rows("llama.cpp:a", "llama.cpp · Qwen3-VL 4B A", 0, "gpu", {("512", "q8"): (2500, 300), ("4096", "q8"): (2000, 290)})
raw += ["ppl|a|15.36", "ppl|b|10.93", "ppl|c|25.75", "ppl|d|9.50", "ppl|llama.cpp:a|15.38"]

with tempfile.TemporaryDirectory() as tmp:
    rawp, jsonp, docp, servp = (os.path.join(tmp, f) for f in ("essai.raw", "conseil.json", "doc.md", "service.json"))
    json.dump({"model": "Qwen3-VL 4B A", "gulliver": {"load_s": 1.7, "long_drafts": {"read": 729.0, "write": 33.8},
               "long_plain": {"read": 728.0, "write": 36.1}, "four": {"total_write": 235.4},
               "three_docs": {"total_s": 15.4, "mean_first_token_s": 6.8},
               "largest": {"context": 131072, "refused": 262144}},
               "llama": {"load_s": 1.1, "long_plain": {"read": 768.0, "write": 34.4}, "four": {"total_write": 229.6},
                         "three_docs": {"total_s": 12.4, "mean_first_token_s": 3.2}}}, open(servp, "w"))
    open(rawp, "w", encoding="utf-8").write("\n".join(raw) + "\n")
    doc = "avant\n<!-- semaine : début -->\nancien\n<!-- semaine : fin -->\naprès\n"
    open(docp, "w", encoding="utf-8").write(doc)
    out = subprocess.run([sys.executable, "-I", os.path.join(HERE, "conseil_modeles.py"), rawp, "--previous", "a",
                          "--new", "b", "--machine", "Mac|M5 Pro, 64 Go|27.2|6.29.0|1 au début, 2 à la fin|30 au début",
                          "--temperature", "50.0|60.0|70.0|5|19|40|0|63|99|3", "--json", jsonp, "--doc", docp, "--service", servp],
                         capture_output=True, text=True)
    check(out.returncode == 0, "conseil_modeles.py s'exécute (" + out.stderr.strip()[-200:] + ")")
    page = out.stdout
    advice = json.load(open(jsonp, encoding="utf-8"))["advice"]
    roles = {a["role"]: a["id"] for a in advice}
    check(roles.get("general") == "b", "pour tout faire : B, le plus juste des Qwen3 qui voient les images et écrivent vite")
    check(roles.get("accurate") == "d", "le plus juste : D, même trop lent pour tout faire")
    check(roles.get("fastest") == "c", "le plus rapide : C (llama.cpp, plus rapide, n'est pas conseillé)")
    check(all(":" not in a["id"] for a in advice), "llama.cpp et MLX ne sont jamais conseillés")
    kv = {a["id"]: a["settings"]["kv_type"] for a in advice}
    check(kv.get("b") == "f16" and kv.get("c") == "q8", "mémoire : demi-précision pour B (+5 %), 8 bits pour C")
    check("llama.cpp · Qwen3-VL 4B A" in page, "llama.cpp figure dans le tableau")
    check("(choix précédent)" in page and "(nouveau)" in page, "le choix précédent et le nouveau sont signalés")
    check("le conseil pour tout faire passe de Qwen3-VL 4B A à Qwen3-VL 30B B (MoE)" in page, "le changement de conseil est dit")
    check("60,0 °C en moyenne, de 50,0 à 70,0 °C" in page and "63 % en moyenne, de 0 à 99 %" in page,
          "la température et la charge : minimum, moyenne, maximum")
    check("| Conversation de 32k : écriture, sans brouillons | 36,1 jetons/s | 34,4 jetons/s |" in page
          and "| 3 documents à la fois : attente moyenne du premier mot | 6,8 s | 3,2 s |" in page
          and "131 072 tokens (262 144 refusés)" in page, "le tableau « comme un utilisateur », Gulliver et llama.cpp")
    after = open(docp, encoding="utf-8").read()
    check("| Chargement du modèle (jusqu'à une première réponse) | 1,7 s |" in after and "3,2 s" not in after,
          "dans le document, le tableau « comme un utilisateur » de Gulliver seul")
    check(after.startswith("avant\n<!-- semaine : début -->") and after.endswith("<!-- semaine : fin -->\naprès\n")
          and "ancien" not in after and "## La mesure de la semaine" in after,
          "la section du document est réécrite entre ses marqueurs, le reste intact")
    check("llama.cpp" not in after.split("Conseillé")[0], "la section du document ne montre que Gulliver")

print("test_conseil: " + ("all checks passed" if failures == 0 else f"{failures} checks failed"))
sys.exit(1 if failures else 0)
