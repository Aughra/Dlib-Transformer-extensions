#!/usr/bin/env python3
# Copyright (C) 2026  papy cyber
# License: Boost Software License   See LICENSE.txt for the full license.
# La veille des modèles de la semaine (fork pour macOS, 2026-11-22 ; veille sans téléchargement depuis le 2026-11-23,
# décision de l'humain : « tu surveilles si des modèles changent de version ou apparaissent, tu m'envoies un mail pour
# que je décide si on les intègre ou pas »).  Lancé chaque lundi à 1 h par macos/modeles-semaine/.
#
#   python3 tools/modeles_semaine.py [GULLIVER_HOME] [--essai]
#
# Par le service qui tourne sur ce Mac (https://127.0.0.1:<port de ses réglages>, certificat du service accepté sans
# vérification : c'est le même Mac), il relève, sans rien télécharger :
#   1. les modèles du catalogue installés dont le catalogue donne une autre empreinte que celle du fichier (une
#      nouvelle version) ;
#   2. les modèles ajoutés depuis Hugging Face dont le fichier a changé là-bas (empreinte de l'en-tête X-Linked-Etag,
#      sans télécharger le fichier) ;
#   3. les modèles du catalogue que le Mac n'a pas et qui y tiendraient : une architecture que le moteur lit, un
#      fichier (avec son projecteur) d'au plus les trois quarts de la mémoire, un modèle qui répond aux questions.
# Les nouveautés pas encore annoncées partent dans un mail à l'administrateur de Gulliver (son adresse dans la base),
# par le Bridge du Mac, avec les réglages « Mail des comptes » du service (expéditeur, port, mot de passe de
# mail.secret, jamais affiché).  build/bench/hebdo/annonces.json garde ce qui a été annoncé, pour ne pas répéter la même
# nouveauté chaque semaine.  Rien n'est installé : l'humain décide, puis on télécharge depuis l'onglet Modèles.
# Avec --essai, le mail est affiché au lieu d'être envoyé, et rien n'est noté.
import email.header
import email.utils
import json
import os
import smtplib
import sqlite3
import ssl
import subprocess
import sys
import time
import urllib.request
from email.mime.text import MIMEText

# Les architectures que le moteur lit (engine/model_config.cpp, read_model_config).
ARCHS = {"llama", "mistral", "mistral3", "granite", "qwen2", "qwen3", "qwen3vl", "mixtral", "granitemoe", "qwen3moe",
         "qwen3vlmoe", "qwen35moe", "phi3", "qwen35", "gemma4", "gemma3", "lfm2", "lfm2moe"}
ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../../../.."))
ANNOUNCED = os.path.join(ROOT, "build/bench/hebdo/annonces.json")


def say(text):
    print(time.strftime("%Y-%m-%d %H:%M ") + text, flush=True)


class Service:
    def __init__(self, home):
        self.settings = {}
        try:
            with open(os.path.join(home, "settings.json"), encoding="utf-8") as f:
                self.settings = json.load(f)
        except (OSError, ValueError):
            pass
        self.base = f"https://127.0.0.1:{int(self.settings.get('port', 18443))}"
        self.context = ssl.create_default_context()
        self.context.check_hostname = False
        self.context.verify_mode = ssl.CERT_NONE

    def call(self, method, path, body=None):
        data = json.dumps(body).encode() if body is not None else (b"{}" if method == "POST" else None)
        req = urllib.request.Request(self.base + path, data=data, method=method,
                                     headers={"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, context=self.context, timeout=60) as r:
                return json.load(r)
        except urllib.error.HTTPError as e:
            try:
                return json.load(e)
            except ValueError:
                return {"ok": False, "code": str(e.code)}


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def hub_sha(url):
    """L'empreinte SHA-256 actuelle d'un fichier de Hugging Face (en-tête X-Linked-Etag), sans le télécharger."""
    opener = urllib.request.build_opener(NoRedirect)
    req = urllib.request.Request(url, method="HEAD", headers={"User-Agent": "gulliver-models-update"})
    try:
        r = opener.open(req, timeout=30)
        headers = r.headers
    except urllib.error.HTTPError as e:
        headers = e.headers
    except Exception:
        return None
    etag = (headers.get("X-Linked-Etag") or headers.get("ETag") or "").strip('"').strip("W/").strip('"')
    return etag if len(etag) == 64 else None


def findings(service, budget):
    """Les nouveautés : une liste de (clé, texte).  La clé change avec la version, pour annoncer chaque version une fois."""
    out = []
    for m in service.call("GET", "/gulliver/v1/models").get("models", []):
        mid, size = m["id"], m.get("total_size") or m.get("size") or 0
        name = m.get("name") or mid
        if m.get("origin") == "huggingface":
            if not m.get("installed") or not m.get("files"):
                continue
            url = m["files"][0].get("url", "")
            sha = hub_sha(url) if "/resolve/" in url else None
            if sha and sha != m.get("sha256"):
                out.append((f"hub:{mid}:{sha}", f"{name} : nouvelle version sur Hugging Face ({m.get('source', '')})."))
            continue
        if m.get("installed"):
            if m.get("local_sha256") and m.get("sha256") and m["local_sha256"] != m["sha256"]:
                out.append((f"version:{mid}:{m['sha256']}",
                            f"{name} : nouvelle version dans le catalogue ({size / 1e9:.1f} Go)."))
            continue
        roles = m.get("roles") or []
        if m.get("arch") in ARCHS and "embedding" not in roles and size and size <= budget:
            out.append((f"nouveau:{mid}:{m.get('sha256', '')}",
                        f"{name} : nouveau modèle qui tiendrait dans le Mac ({size / 1e9:.1f} Go, {m.get('arch')})."))
    return out


def admin_address(home):
    try:
        db = sqlite3.connect(f"file:{os.path.join(home, 'gulliver.db')}?mode=ro", uri=True)
        row = db.execute("SELECT email FROM profiles WHERE admin = 1 AND email <> '' LIMIT 1").fetchone()
        db.close()
        return row[0] if row else ""
    except sqlite3.Error:
        return ""


def send(service, home, to, subject, body):
    """Le mail par le Bridge du Mac, avec les réglages « Mail des comptes » du service (comme host/mailer.cpp)."""
    sender, port = service.settings.get("mail_from", ""), int(service.settings.get("mail_port", 1025))
    with open(os.path.join(home, "mail.secret"), encoding="utf-8") as f:
        secret = json.load(f).get("secret", "")
    message = MIMEText(body, "plain", "utf-8")
    message["Subject"] = email.header.Header(subject, "utf-8")
    message["From"] = email.utils.formataddr(("Gulliver", sender))
    message["To"] = to
    message["Date"] = email.utils.formatdate(localtime=True)
    message["Message-ID"] = email.utils.make_msgid(domain=sender.split("@")[-1])
    with smtplib.SMTP("127.0.0.1", port, timeout=30) as smtp:
        smtp.login(sender, secret)
        smtp.sendmail(sender, [to], message.as_string())


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    trial = "--essai" in sys.argv
    home = args[0] if args else os.environ.get("GULLIVER_HOME", "")
    service = Service(home)
    memory = int(subprocess.run(["sysctl", "-n", "hw.memsize"], capture_output=True, text=True).stdout.strip() or 0)
    budget = memory * 3 // 4
    if service.call("GET", "/gulliver/v1/models").get("offline"):
        say("le service est hors ligne : rien à faire")
        return
    service.call("POST", "/gulliver/v1/models/refresh")
    for _ in range(60):
        time.sleep(5)
        if (service.call("GET", "/gulliver/v1/models").get("catalog") or {}).get("state") != "checking":
            break
    found = findings(service, budget)
    try:
        with open(ANNOUNCED, encoding="utf-8") as f:
            announced = set(json.load(f).get("keys", []))
    except (OSError, ValueError):
        announced = set()
    fresh = [(k, t) for k, t in found if k not in announced]
    say(f"{len(found)} nouveautés en tout, {len(fresh)} pas encore annoncées ; au plus {budget / 2**30:.0f} Go par modèle")
    for _, text in fresh:
        say("  " + text)
    if not fresh:
        return
    body = ("Bonjour,\n\nLa veille des modèles de Gulliver a trouvé ceci cette semaine :\n\n"
            + "".join(f"  - {t}\n" for _, t in fresh)
            + "\nRien n'a été téléchargé. Pour en intégrer un : onglet Modèles de Gulliver (bouton Télécharger), "
              "ou demandez-le à Claude. Chaque nouveauté n'est annoncée qu'une fois.\n\nGulliver\n")
    subject = f"Gulliver : {len(fresh)} nouveauté{'s' if len(fresh) > 1 else ''} dans les modèles"
    if trial:
        print("\n--- essai : mail non envoyé ---\nObjet : " + subject + "\n\n" + body)
        return
    to = admin_address(home)
    if not to:
        say("l'administrateur n'a pas d'adresse mail : rien n'est envoyé")
        return
    try:
        send(service, home, to, subject, body)
    except (OSError, smtplib.SMTPException, ValueError) as e:
        say(f"mail pas envoyé : {type(e).__name__}")
        return
    os.makedirs(os.path.dirname(ANNOUNCED), exist_ok=True)
    with open(ANNOUNCED, "w", encoding="utf-8") as f:
        json.dump({"date": time.strftime("%Y-%m-%d %H:%M"), "keys": sorted(announced | {k for k, _ in fresh})}, f,
                  ensure_ascii=False, indent=1)
    say("mail envoyé à l'administrateur")


if __name__ == "__main__":
    main()
