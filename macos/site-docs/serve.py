#!/usr/bin/env python3
"""
Site de lecture des documents du fork, pour le réseau de la maison.

Sert, en lecture seule, une liste fermée de fichiers du dépôt (docs/, blog/, logs/,
quelques README et les notes de version d'Albireo) : rien d'autre n'est accessible,
ni le reste du dépôt, ni les dossiers cachés. Les fichiers Markdown sont mis en page
dans le navigateur par marked et DOMPurify, repris des fichiers d'Albireo (aucun accès
à Internet).

    python3 macos/site-docs/serve.py [--host IP] [--port 8650]

Sans --host, l'adresse est celle de l'interface réseau par défaut du Mac.
"""

import argparse
import html
import http.server
import mimetypes
import re
import subprocess
import urllib.parse
from pathlib import Path

RACINE = Path(__file__).resolve().parents[2]
VENDOR = RACINE / "dlib/dlib/external/albireo/web/vendor"

# Les fichiers servis : motifs relatifs à la racine du dépôt. Liste fermée.
MOTIFS = [
    "docs/*.md", "docs/*.pdf", "docs/captures/*.md", "docs/captures/*.jpg", "docs/captures/*.png",
    "blog/*.md", "blog/images/*",
    "logs/*.md",
    "CLAUDE.md",
    "macos/metal-sonde/README.md",
    "macos/site-docs/README.md",
    "dlib/dlib/external/albireo/README.md",
    "dlib/dlib/external/albireo/web/changelog.md",
    "dlib/dlib/external/albireo/tools/page_test/README.md",
    "dlib/dlib/external/pseudalis/README.md",
]
VENDOR_FICHIERS = {
    "marked.esm.js": VENDOR / "marked/marked.esm.js",
    "purify.es.mjs": VENDOR / "dompurify/purify.es.mjs",
}

# Sommaire : (titre de la section, [(chemin, titre, description)]).
SOMMAIRE = [
    ("Pour commencer", [
        ("docs/differences-mac-windows.md", "Différences avec Windows", "Tout ce que le fork fait autrement que l'original, en tableaux."),
        ("logs/reprise.md", "Où en est le chantier", "Fait, en cours, à faire."),
    ]),
    ("Le portage", [
        ("docs/memo-portage-macos-arm64.md", "Mémo de portage", "Commandes de build, service, état constaté."),
        ("docs/notes-pour-l-auteur.md", "Notes pour l'auteur", "Bugs de l'original et gains de vitesse."),
        ("docs/mac-mini-premier-jour.md", "Premier jour sur le Mac mini", "La liste à suivre à l'arrivée du M5 Pro."),
        ("macos/metal-sonde/README.md", "La sonde Metal", "Le petit programme qui teste le GPU."),
        ("CLAUDE.md", "Règles du fork", "Le document d'autorité."),
    ]),
    ("Journaux", [
        ("logs/journal-modifications.md", "Journal des modifications", "L'historique, le plus récent en haut."),
        ("logs/sieste-2026-11-11.md", "Pendant la sieste (11 nov.)", "Les choix faits seul."),
        ("logs/nuit-2026-11-12.md", "Pendant la nuit (12-13 nov.)", "Les choix faits seul."),
    ]),
    ("Albireo et Pseudalis", [
        ("dlib/dlib/external/albireo/web/changelog.md", "Notes de version d'Albireo", "Ce que chaque version apporte."),
        ("docs/captures/README.md", "Captures d'écran", "Albireo sur le Mac et sur le téléphone."),
        ("dlib/dlib/external/albireo/tools/page_test/README.md", "Tests de la page", "Le banc d'essai de l'interface."),
        ("dlib/dlib/external/albireo/README.md", "README d'Albireo", "De l'original, en anglais."),
        ("dlib/dlib/external/pseudalis/README.md", "README de Pseudalis", "De l'original, en anglais."),
    ]),
    ("Articles de l'original (anglais)", [
        ("docs/01-introduction.md", "A Small Language Model Stack", "La chaîne complète, du corpus au modèle servi."),
        ("docs/buffer-level-residency.pdf", "Buffer-level residency (PDF)", "Rapport sur la gestion de la mémoire."),
    ]),
]

STYLE = """
:root { --fond:#fbfaf7; --texte:#1f2328; --doux:#656d76; --lien:#0b5cad; --bord:#d8dee4; --code:#f1f1ee; --carte:#fff; }
@media (prefers-color-scheme: dark) { :root { --fond:#16181c; --texte:#e6e6e3; --doux:#9aa1a9; --lien:#7cb4ff; --bord:#30363d; --code:#22262c; --carte:#1d2026; } }
* { box-sizing:border-box; }
body { margin:0; background:var(--fond); color:var(--texte); font:17px/1.6 -apple-system, BlinkMacSystemFont, "Helvetica Neue", sans-serif; }
main { max-width:760px; margin:0 auto; padding:16px 16px 64px; }
a { color:var(--lien); }
header.barre { position:sticky; top:0; background:var(--fond); border-bottom:1px solid var(--bord); padding:10px 16px; font-size:15px; z-index:1; }
h1 { font-size:1.6em; line-height:1.25; } h2 { font-size:1.3em; margin-top:1.8em; } h3 { font-size:1.1em; }
pre { background:var(--code); padding:12px; overflow-x:auto; border-radius:8px; font-size:14px; }
code { background:var(--code); padding:1px 4px; border-radius:4px; font-size:.9em; overflow-wrap:anywhere; }
pre code { padding:0; overflow-wrap:normal; }
table { border-collapse:collapse; display:block; overflow-x:auto; font-size:15px; margin:1em 0; }
th, td { border:1px solid var(--bord); padding:6px 8px; vertical-align:top; }
img { max-width:100%; height:auto; }
blockquote { margin:1em 0; padding:0 1em; border-left:4px solid var(--bord); color:var(--doux); }
.carte { display:block; background:var(--carte); border:1px solid var(--bord); border-radius:10px; padding:12px 14px; margin:8px 0; text-decoration:none; color:var(--texte); }
.carte b { color:var(--lien); } .carte span { display:block; color:var(--doux); font-size:15px; }
ol.episodes { padding-left:1.6em; } ol.episodes li { margin:6px 0; }
.doux { color:var(--doux); font-size:15px; }
"""


def fichiers_permis():
    permis = set()
    for motif in MOTIFS:
        for p in RACINE.glob(motif):
            if p.is_file() and not p.is_symlink():
                permis.add(p.resolve().relative_to(RACINE).as_posix())
    return permis


def titre_md(chemin):
    try:
        for ligne in (RACINE / chemin).read_text(encoding="utf-8").splitlines():
            if ligne.startswith("# "):
                return ligne[2:].strip()
    except OSError:
        pass
    return chemin


def page(titre, corps, barre=True):
    entete = '<header class="barre"><a href="/">← Sommaire</a></header>' if barre else ""
    return (f'<!doctype html><html lang="fr"><head><meta charset="utf-8">'
            f'<meta name="viewport" content="width=device-width, initial-scale=1">'
            f'<link rel="icon" href="data:,"><title>{html.escape(titre)}</title><style>{STYLE}</style></head>'
            f'<body>{entete}<main>{corps}</main></body></html>')


def sommaire(permis):
    blocs = ["<h1>Documents du fork Mac</h1>",
             '<p class="doux">Dlib Transformer Extensions sur macOS, à lire depuis le canapé.</p>']
    for section, entrees in SOMMAIRE:
        blocs.append(f"<h2>{html.escape(section)}</h2>")
        for chemin, titre, desc in entrees:
            if chemin in permis:
                blocs.append(f'<a class="carte" href="{lien(chemin)}"><b>{html.escape(titre)}</b>'
                             f'<span>{html.escape(desc)}</span></a>')
    episodes = sorted(c for c in permis if re.match(r"blog/\d+-.*\.md$", c))
    blocs.append(f"<h2>Le blog ({len(episodes)} épisodes)</h2><ol class=\"episodes\">")
    for c in episodes:
        t = re.sub(r"^Dlib Transformer Extensions sur macOS, épisode \d+ : ", "", titre_md(c))
        blocs.append(f'<li value="{int(c[5:7])}"><a href="{lien(c)}">{html.escape(t)}</a></li>')
    blocs.append("</ol>")
    return page("Documents du fork Mac", "".join(blocs), barre=False)


def lien(chemin):
    return ("/voir/" if chemin.endswith(".md") else "/brut/") + urllib.parse.quote(chemin)


def vue_markdown(chemin):
    # Le texte est rendu dans le navigateur ; les liens relatifs sont réécrits vers /voir/ ou /brut/.
    script = """
import { Marked } from '/vendor/marked.esm.js';
import DOMPurify from '/vendor/purify.es.mjs';
const chemin = %s;
const dossier = chemin.includes('/') ? chemin.slice(0, chemin.lastIndexOf('/') + 1) : '';
function resoudre(href) {
  if (/^([a-z]+:|#|\\/)/i.test(href)) return null;
  const u = new URL(href, 'http://x/' + dossier);
  return { chemin: decodeURIComponent(u.pathname.slice(1)), ancre: u.hash };
}
const texte = await (await fetch('/brut/' + encodeURIComponent(chemin).replace(/%%2F/g, '/'))).text();
const html = DOMPurify.sanitize(new Marked({ gfm: true }).parse(texte));
const main = document.querySelector('main');
main.innerHTML = html;
for (const a of main.querySelectorAll('a[href]')) {
  const r = resoudre(a.getAttribute('href'));
  if (r) a.href = (r.chemin.endsWith('.md') ? '/voir/' : '/brut/') + r.chemin + r.ancre;
  else if (/^https?:/.test(a.getAttribute('href'))) a.target = '_blank';
}
for (const img of main.querySelectorAll('img[src]')) {
  const r = resoudre(img.getAttribute('src'));
  if (r) img.src = '/brut/' + r.chemin;
}
const h1 = main.querySelector('h1');
if (h1) document.title = h1.textContent;
for (const h of main.querySelectorAll('h1,h2,h3,h4')) {
  h.id = h.textContent.trim().toLowerCase().replace(/[^\\p{L}\\p{N}\\s-]/gu, '').replace(/\\s/g, '-');
}
if (location.hash) document.getElementById(decodeURIComponent(location.hash.slice(1)))?.scrollIntoView();
""" % _js_chaine(chemin)
    corps = f'<p class="doux">Chargement…</p><script type="module">{script}</script>'
    return page(chemin, corps)


def _js_chaine(s):
    import json
    return json.dumps(s).replace("</", "<\\/")


class Gestionnaire(http.server.BaseHTTPRequestHandler):
    server_version = "site-docs"

    def envoyer(self, code, type_, donnees):
        self.send_response(code)
        self.send_header("Content-Type", type_)
        self.send_header("Content-Length", str(len(donnees)))
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("Content-Security-Policy",
                         "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'unsafe-inline'; "
                         "img-src 'self' data:; frame-ancestors 'none'")
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(donnees)

    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        chemin = urllib.parse.unquote(urllib.parse.urlsplit(self.path).path)
        permis = fichiers_permis()
        if chemin == "/":
            return self.envoyer(200, "text/html; charset=utf-8", sommaire(permis).encode())
        if chemin.startswith("/vendor/") and chemin[8:] in VENDOR_FICHIERS:
            return self.envoyer(200, "text/javascript; charset=utf-8", VENDOR_FICHIERS[chemin[8:]].read_bytes())
        for prefixe in ("/voir/", "/brut/"):
            if chemin.startswith(prefixe):
                rel = chemin[len(prefixe):]
                if rel not in permis:
                    break
                if prefixe == "/voir/" and rel.endswith(".md"):
                    return self.envoyer(200, "text/html; charset=utf-8", vue_markdown(rel).encode())
                type_ = "text/plain; charset=utf-8" if rel.endswith(".md") else (
                    mimetypes.guess_type(rel)[0] or "application/octet-stream")
                return self.envoyer(200, type_, (RACINE / rel).read_bytes())
        self.envoyer(404, "text/html; charset=utf-8", page("Introuvable", "<h1>Introuvable</h1>").encode())

    def log_message(self, fmt, *args):
        print("%s - %s" % (self.address_string(), fmt % args), flush=True)


def ip_locale():
    interface = subprocess.run(["route", "-n", "get", "default"], capture_output=True, text=True).stdout
    m = re.search(r"interface:\s*(\S+)", interface)
    if m:
        ip = subprocess.run(["ipconfig", "getifaddr", m.group(1)], capture_output=True, text=True).stdout.strip()
        if ip:
            return ip
    return "127.0.0.1"


def main():
    ap = argparse.ArgumentParser(description="Site de lecture des documents du fork")
    ap.add_argument("--host", default=None)
    ap.add_argument("--port", type=int, default=8650)
    args = ap.parse_args()
    hote = args.host or ip_locale()
    serveur = http.server.ThreadingHTTPServer((hote, args.port), Gestionnaire)
    print(f"Documents du fork : http://{hote}:{args.port}/", flush=True)
    serveur.serve_forever()


if __name__ == "__main__":
    main()
