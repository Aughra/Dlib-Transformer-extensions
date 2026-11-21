"""La perplexité d'un modèle MLX sur un texte, découpée comme llama-perplexity et
perplexity_runtime (fork pour macOS, 2026-11-21), pour comparer la justesse des moteurs :

    build/mlx-venv/bin/python perplexity_mlx.py <dossier du modèle MLX> texte.txt [contexte=512] [morceaux=0]

Le texte est tokenisé en entier (BOS seulement si le tokenizer en ajoute un), coupé en morceaux
de « contexte » tokens ; seules les prédictions de la seconde moitié de chaque morceau comptent.
"""
import math
import sys

import mlx.core as mx
from mlx_lm import load


def main():
    path, text_path = sys.argv[1], sys.argv[2]
    ctx = int(sys.argv[3]) if len(sys.argv) > 3 else 512
    chunks = int(sys.argv[4]) if len(sys.argv) > 4 else 0
    model, tok = load(path)
    with open(text_path, encoding="utf-8") as f:
        text = f.read()
    tokens = tok.encode(text)
    bos = tok.bos_token_id if tokens and tokens[0] == tok.bos_token_id else None
    available = len(tokens) // ctx
    if chunks <= 0 or chunks > available:
        chunks = available
    first = ctx // 2
    nll = nll2 = 0.0
    count = top1 = 0
    for c in range(chunks):
        chunk = tokens[c * ctx:(c + 1) * ctx]
        if bos is not None:
            chunk[0] = bos
        logits = model(mx.array([chunk]))[0].astype(mx.float32)
        lp = logits - mx.logsumexp(logits, axis=-1, keepdims=True)
        rows = lp[first:ctx - 1]
        nexts = mx.array(chunk[first + 1:ctx])
        picked = mx.take_along_axis(rows, nexts[:, None], axis=-1)[:, 0]
        best = mx.argmax(rows, axis=-1)
        mx.eval(picked, best)
        for v in picked.tolist():
            nll -= v
            nll2 += v * v
            count += 1
        top1 += int(mx.sum(best == nexts).item())
        print(f"[{c + 1}] {math.exp(nll / count):.4f}", flush=True)
    mean = nll / count
    var = nll2 / count - mean * mean
    err = math.exp(mean) * math.sqrt(max(0.0, var) / max(1, count - 1))
    print(f"tokens: {len(tokens)}, chunks: {chunks} of {ctx}, scored positions: {count}")
    print(f"perplexity: {math.exp(mean):.4f} +/- {err:.4f}")
    print(f"top-1: {100.0 * top1 / count:.2f} %")


if __name__ == "__main__":
    main()
