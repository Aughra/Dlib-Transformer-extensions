#!/usr/bin/env python3
# Copyright (C) 2026  papy cyber
# License: Boost Software License   See LICENSE.txt for the full license.
"""L'épreuve des modèles de Gulliver : une note sur 100, comparable entre toutes les familles (fork pour macOS,
2026-11-24, demande de l'humain).

    python3 tools/epreuve_modeles.py fichier.gguf [--json sortie.json] [--questions epreuve_questions.json]

La perplexité ne compare que des modèles qui découpent le texte de la même façon, et bien deviner un texte n'est pas
bien répondre.  Ici, le modèle reçoit les 40 questions de epreuve_questions.json (calcul, logique, connaissances,
français, consignes, compréhension, code), toutes à la fois, par le moteur de Gulliver (tests/engine_chat, GPU,
température 0, raisonnement borné), avec une consigne : finir chaque réponse par une ligne « Réponse : … ».  La note
se lit sur cette ligne (à défaut, sur la dernière ligne de la réponse), selon le type de la question.  Écrit
« epreuve: <note> <justes>/<total> <secondes> » en dernière ligne, et le détail en JSON avec --json.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time
import unicodedata

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "../../../../.."))
# EPREUVE_BUILD : un autre dossier de build de Gulliver (par défaut build/macos-gulliver du dépôt).
BUILD = os.environ.get("EPREUVE_BUILD") or os.path.join(ROOT, "build", "macos-gulliver")
CHAT = os.path.join(BUILD, "tests", "engine_chat")
ENGINE = os.path.join(BUILD, "bin", "engines", "libgulliver-engine-cpu.dylib")
# La consigne envoyée au modèle (en anglais, comme les autres consignes internes).
SYSTEM = ("Answer in French, briefly. End your answer with a last line of the form « Réponse : <your answer> », "
          "with only the answer on that line.")
MARK = re.compile(r"r[ée]ponse(?:\s+finale)?\s*:\s*", re.IGNORECASE)
NUMBER = re.compile(r"-?\d{1,3}(?:[   ]\d{3})+(?:[.,]\d+)?|-?\d+(?:[.,]\d+)?")


def plain(text):
    """Sans accents ni majuscules, apostrophes droites : pour comparer des mots."""
    t = unicodedata.normalize("NFD", text.replace("’", "'").lower())
    return "".join(c for c in t if unicodedata.category(c) != "Mn")


def final_answer(block):
    """La réponse finale : ce qui suit la dernière « Réponse : », sinon la dernière ligne non vide."""
    marks = list(MARK.finditer(block))
    if marks:
        rest = block[marks[-1].end():].strip()
        line = rest.split("\n", 1)[0].strip() if rest else ""
    else:
        lines = [x.strip() for x in block.strip().split("\n") if x.strip()]
        line = lines[-1] if lines else ""
    # Les formules : \frac{1}{6} se lit 1/6, et les délimiteurs \( \) $ et les chevrons s'effacent.
    line = re.sub(r"\\d?frac\{([^{}]*)\}\{([^{}]*)\}", r"\1/\2", line)
    line = re.sub(r"\\[()\[\]]|\$|\\boxed\{([^{}]*)\}", r"\1", line)
    line = re.sub(r"[*_`«»\"“”<>]", "", line).strip()
    return line.rstrip(" .!").strip()


def to_number(s):
    return float(re.sub(r"[   ]", "", s).replace(",", "."))


def grade(q, answer):
    kind = q["type"]
    if kind == "nombre":
        m = NUMBER.search(answer)
        return bool(m) and abs(to_number(m.group(0)) - float(q["attendu"])) < 1e-6
    if kind == "exact":
        return answer in q["attendu"]
    if kind == "motif":
        return re.match(q["attendu"], answer) is not None
    text = plain(answer)
    has = lambda w: re.search(r"(?<![a-z0-9])" + re.escape(plain(w)) + r"(?![a-z0-9])", text) is not None
    return any(has(w) for w in q["attendu"]) and not any(has(w) for w in q.get("interdit", []))


def blocks(output, questions):
    """Le texte de chaque réponse dans la sortie d'engine_chat : « === question », la réponse, « --- {…} »."""
    found = {}
    for part in output.split("\n=== ")[1:]:
        head, _, body = part.partition("\n")
        end = body.rfind("\n--- ")
        found[head.strip()] = body[:end] if end >= 0 else body
    return [found.get(q["question"], "") for q in questions]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model")
    ap.add_argument("--json")
    ap.add_argument("--questions", default=os.path.join(HERE, "epreuve_questions.json"))
    ap.add_argument("--timeout", type=int, default=900)
    args = ap.parse_args()
    with open(args.questions, encoding="utf-8") as f:
        questions = json.load(f)["questions"]
    cmd = [CHAT, ENGINE, args.model, "--parallel", "--temperature", "0", "--max", "2048", "--budget", "1024",
           "--context", "8192", "--system", SYSTEM] + [q["question"] for q in questions]
    t0 = time.time()
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, errors="replace", timeout=args.timeout).stdout
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or b"").decode("utf-8", "replace") if isinstance(e.stdout, bytes) else (e.stdout or "")
    seconds = time.time() - t0
    details, right = [], 0
    for q, block in zip(questions, blocks(out, questions)):
        answer = final_answer(block) if block else ""
        ok = bool(block) and grade(q, answer)
        right += ok
        details.append({"id": q["id"], "theme": q["theme"], "ok": ok, "answer": answer[:200]})
    score = round(100 * right / len(questions))
    for d in details:
        print(f"{'juste' if d['ok'] else 'faux '}  {d['id']:<11} {d['answer'][:80]}")
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump({"model": os.path.basename(args.model), "score": score, "right": right, "total": len(questions),
                       "seconds": round(seconds, 1), "details": details}, f, ensure_ascii=False, indent=1)
    print(f"epreuve: {score} {right}/{len(questions)} {seconds:.0f}")
    return 0 if any(b for b in (d["answer"] for d in details)) else 1


if __name__ == "__main__":
    sys.exit(main())
