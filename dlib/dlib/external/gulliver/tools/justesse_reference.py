#!/usr/bin/env python3
# Copyright (C) 2026  papy cyber
# License: Boost Software License   See LICENSE.txt for the full license.
#
# Le juge de justesse de référence (fork pour macOS, 2026-11-22) : Gulliver comparé au modèle d'origine.
#
#   build/torch-venv/bin/python tools/justesse_reference.py modele.gguf dossier-hf [--text texte.txt] [--ctx 512]
#       [--chunks 20] [--modes cpu,metal,metal-steps] [--json sortie.json]
#
# La perplexité contre llama.cpp compare deux copies compressées entre elles.  Ce juge compare Gulliver au modèle
# publié par ses auteurs, tel que Hugging Face le distribue (poids en pleine précision), calculé en float32 par PyTorch
# (sur le GPU du Mac, MPS) : la référence.  Il ne sert qu'à mesurer ; Gulliver n'en dépend pas.  PyTorch et
# transformers vivent dans un environnement à part (build/torch-venv, jamais commité) :
#
#   /opt/homebrew/bin/python3.13 -m venv build/torch-venv
#   build/torch-venv/bin/pip install torch transformers safetensors accelerate huggingface_hub
#   build/torch-venv/bin/hf download Qwen/Qwen3-VL-4B-Thinking --local-dir build/hf-models/qwen3-vl-4b-thinking
#
# Gulliver (tests/perplexity_runtime, mémoire de conversation en demi-précision) écrit pour chaque position notée la
# log-probabilité du token suivant et ses 20 candidats préférés (GULLIVER_PPL_DUMP) ; le modèle d'origine lit les mêmes
# tokens.  Mesures, sur les positions de la seconde moitié de chaque morceau (comme llama-perplexity) :
#   - les deux perplexités et leur écart ;
#   - l'accord du premier choix : la part des positions où Gulliver et l'origine préfèrent le même token ;
#   - l'écart moyen de log-probabilité du vrai token suivant ;
#   - la divergence KL(Gulliver ‖ origine) sur les 20 candidats de Gulliver (la masse hors de ces 20 candidats est
#     ignorée : une borne basse, assez juste quand ces candidats portent presque toute la probabilité, ce qui est dit) ;
#   - si les deux découpent le texte en tokens de la même façon.
import argparse
import json
import math
import os
import subprocess
import sys
import time

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../../../.."))
PPL = os.path.join(ROOT, "build", "macos-gulliver", "tests", "perplexity_runtime")
WORK = os.path.join(ROOT, "build", "justesse-reference")


def gulliver_dump(gguf, text, ctx, chunks, mode, out):
    """Lance perplexity_runtime et rend (sa sortie, le chemin du fichier position par position)."""
    # metal-steps : la seconde moitié de chaque morceau token par token sur le GPU, le chemin de l'écriture d'une réponse.
    args = [PPL, gguf, text, str(ctx), str(chunks), "f16"] + {"metal": ["metal"], "metal-steps": ["metal", "steps"]}.get(mode, [])
    r = subprocess.run(args, env=dict(os.environ, GULLIVER_PPL_DUMP=out), capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError("perplexity_runtime: " + (r.stdout + r.stderr)[-400:])
    return r.stdout


def read_dump(path):
    chunks, positions = {}, []
    with open(path) as f:
        for line in f:
            d = json.loads(line)
            if "chunk" in d:
                chunks[d["chunk"]] = d
            else:
                positions.append(d)
    return chunks, positions


def load_reference(folder):
    import torch
    import transformers
    device = "mps" if torch.backends.mps.is_available() else "cpu"
    model = None
    for cls in ("AutoModelForCausalLM", "AutoModelForImageTextToText"):
        try:
            model = getattr(transformers, cls).from_pretrained(folder, dtype=torch.float32)
            break
        except (ValueError, KeyError):
            continue
    if model is None:
        raise RuntimeError("transformers cannot load " + folder)
    model.to(device).eval()
    tokenizer = transformers.AutoTokenizer.from_pretrained(folder)
    return torch, model, tokenizer, device


def judge(torch, model, device, chunks, positions):
    """Compare les positions de Gulliver aux probabilités de l'origine ; rend le résumé."""
    by_chunk = {}
    for p in positions:
        by_chunk.setdefault(p["c"], []).append(p)
    nll_g = nll_r = dlp = kl = mass = 0.0
    n = agree = top5 = 0
    with torch.no_grad():
        for c, info in sorted(chunks.items()):
            ids = torch.tensor([info["tokens"]], device=device)
            logits = model(input_ids=ids).logits[0].float()
            logp = torch.log_softmax(logits, dim=-1).cpu()
            for p in by_chunk.get(c, []):
                row = logp[p["p"]]
                ref_next = float(row[p["next"]])
                nll_g -= p["lp"]
                nll_r -= ref_next
                dlp += abs(p["lp"] - ref_next)
                top = p["top"]
                ref_best = int(torch.argmax(row))
                agree += int(top[0][0] == ref_best)
                ref5 = set(torch.topk(row, 5).indices.tolist())
                top5 += len(ref5 & {t[0] for t in top[:5]}) / 5
                for tid, lq in top:
                    q = math.exp(lq)
                    kl += q * (lq - float(row[tid]))
                    mass += q
                n += 1
    return {"positions": n, "perplexity_gulliver": round(math.exp(nll_g / n), 4),
            "perplexity_reference": round(math.exp(nll_r / n), 4),
            "ecart_perplexite_pct": round(100 * (math.exp(nll_g / n) / math.exp(nll_r / n) - 1), 2),
            "accord_premier_choix_pct": round(100 * agree / n, 2), "accord_5_premiers_pct": round(100 * top5 / n, 2),
            "ecart_logprob_moyen": round(dlp / n, 4), "kl_20_candidats": round(kl / n, 5),
            "masse_20_candidats_pct": round(100 * mass / n, 2)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("gguf")
    ap.add_argument("reference", help="le dossier du modèle d'origine (Hugging Face)")
    ap.add_argument("--text", default=os.path.join(ROOT, "build", "bench-moteurs", "texte.txt"))
    ap.add_argument("--ctx", type=int, default=512)
    ap.add_argument("--chunks", type=int, default=20)
    ap.add_argument("--modes", default="cpu,metal")
    ap.add_argument("--json")
    a = ap.parse_args()
    os.makedirs(WORK, exist_ok=True)
    name = os.path.splitext(os.path.basename(a.gguf))[0]
    runs = {}
    for mode in a.modes.split(","):
        out = os.path.join(WORK, f"{name}-{mode}.jsonl")
        t = time.time()
        gulliver_dump(a.gguf, a.text, a.ctx, a.chunks, mode, out)
        print(f"Gulliver ({mode}) : {time.time() - t:.0f} s", flush=True)
        runs[mode] = read_dump(out)
    torch, model, tokenizer, device = load_reference(a.reference)
    print(f"origine chargée sur {device} (float32)", flush=True)
    # Les deux découpent-ils le texte de la même façon ?  Les morceaux de Gulliver mis bout à bout contre le
    # découpage de l'origine (sans token de début, que Qwen ne met pas).
    chunks0 = next(iter(runs.values()))[0]
    ours = [t for c in sorted(chunks0) for t in chunks0[c]["tokens"]]
    theirs = tokenizer(open(a.text, encoding="utf-8").read(), add_special_tokens=False)["input_ids"][:len(ours)]
    same = sum(1 for x, y in zip(ours, theirs) if x == y)
    result = {"model": name, "reference": os.path.basename(os.path.normpath(a.reference)), "ctx": a.ctx,
              "chunks": len(chunks0), "tokens_identiques_pct": round(100 * same / max(1, len(ours)), 2), "modes": {}}
    print(f"tokens identiques : {result['tokens_identiques_pct']} %", flush=True)
    for mode, (chunks, positions) in runs.items():
        result["modes"][mode] = judge(torch, model, device, chunks, positions)
        print(mode, json.dumps(result["modes"][mode], ensure_ascii=False), flush=True)
    if a.json:
        json.dump(result, open(a.json, "w", encoding="utf-8"), ensure_ascii=False, indent=1)


if __name__ == "__main__":
    main()
