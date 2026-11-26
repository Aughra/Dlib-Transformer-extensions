#!/usr/bin/env python3
# Copyright (C) 2026  papy cyber
# License: Boost Software License   See LICENSE.txt for the full license.
"""Un mail à l'administrateur de Gulliver, pour les agents du Mac (fork pour macOS, 2026-11-23).

    from mail_admin import send_admin
    send_admin(home, "Objet", "Texte")          # lève OSError, smtplib.SMTPException ou ValueError

    python3 tools/mail_admin.py GULLIVER_HOME "Objet" < texte

L'adresse est celle du profil administrateur, lue dans la base du service (gulliver.db, en lecture seule) ; le mail
part par Proton Mail Bridge sur 127.0.0.1, avec les réglages « Mail des comptes » du service (mail_from, mail_port
de settings.json, mot de passe de mail.secret, jamais affiché), comme host/mailer.cpp.
"""
import email.header
import email.utils
import json
import os
import smtplib
import sqlite3
import sys
from email.mime.text import MIMEText


def admin_address(home):
    try:
        db = sqlite3.connect(f"file:{os.path.join(home, 'gulliver.db')}?mode=ro", uri=True)
        row = db.execute("SELECT email FROM profiles WHERE admin = 1 AND email <> '' ORDER BY created, rowid LIMIT 1").fetchone()
        db.close()
        return row[0] if row else ""
    except sqlite3.Error:
        return ""


def send_admin(home, subject, body):
    to = admin_address(home)
    if not to:
        raise ValueError("the administrator has no mail address")
    with open(os.path.join(home, "settings.json"), encoding="utf-8") as f:
        settings = json.load(f)
    sender, port = settings.get("mail_from", ""), int(settings.get("mail_port", 1025))
    if not sender:
        raise ValueError("no mail_from in the settings")
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


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: mail_admin.py GULLIVER_HOME SUBJECT < BODY")
    try:
        send_admin(sys.argv[1], sys.argv[2], sys.stdin.read())
    except (OSError, smtplib.SMTPException, ValueError) as e:
        sys.exit(f"mail not sent: {type(e).__name__}: {e}")
