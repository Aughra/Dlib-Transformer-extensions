#!/usr/bin/env python3
# Copyright (C) 2026  papy cyber
# License: Boost Software License   See LICENSE.txt for the full license.
"""Un faux facteur pour les tests des comptes de Gulliver (fork pour macOS, 2026-11-23).

Un petit serveur SMTP sur 127.0.0.1, dans un thread : il accepte tout message et le garde en mémoire au lieu
de l'envoyer.  Les tests règlent mail_port du service d'essai sur son port, puis lisent le code de connexion ou
le lien « mot de passe oublié » du dernier mail reçu par une adresse :

    from faux_smtp import FakeSMTP
    smtp = FakeSMTP()                    # smtp.port
    ...
    code = smtp.code_for("a@b.fr")       # les 8 chiffres du dernier mail vers cette adresse
    token = smtp.reset_for("a@b.fr")     # le jeton du lien #reset=…
    smtp.close()

Il comprend EHLO, AUTH PLAIN (le mot de passe reçu est gardé dans smtp.logins, pour vérifier qu'il arrive),
MAIL, RCPT, DATA, RSET, NOOP et QUIT, ce dont se sert host/mailer.cpp.
"""
import base64
import email
import re
import socketserver
import threading
import time


class _Handler(socketserver.StreamRequestHandler):
    def reply(self, line):
        self.wfile.write((line + "\r\n").encode())

    def handle(self):
        box = self.server.box
        self.reply("220 faux-smtp ready")
        sender, rcpts = None, []
        while True:
            raw = self.rfile.readline()
            if not raw:
                return
            line = raw.decode("utf-8", "replace").rstrip("\r\n")
            verb = line.split(" ", 1)[0].upper()
            if verb in ("EHLO", "HELO"):
                self.wfile.write(b"250-faux-smtp\r\n250-8BITMIME\r\n250 AUTH PLAIN\r\n")
            elif verb == "AUTH":
                parts = line.split(" ")
                try:
                    _, user, password = base64.b64decode(parts[2]).decode().split("\0")
                    box.logins.append((user, password))
                    self.reply("235 ok")
                except Exception:
                    self.reply("535 bad auth")
            elif verb == "MAIL":
                sender, rcpts = line[10:].strip("<>"), []
                self.reply("250 ok")
            elif verb == "RCPT":
                rcpts.append(line[8:].strip("<>"))
                self.reply("250 ok")
            elif verb == "DATA":
                self.reply("354 go")
                data = []
                while True:
                    l = self.rfile.readline()
                    if not l or l in (b".\r\n", b".\n"):
                        break
                    data.append(l)
                message = email.message_from_bytes(b"".join(data))
                body = message.get_payload(decode=True).decode("utf-8")
                subject = str(email.header.make_header(email.header.decode_header(message["Subject"] or "")))
                with box.lock:
                    for r in rcpts:
                        box.mails.append({"from": sender, "to": r.lower(), "subject": subject, "body": body})
                self.reply("250 queued")
            elif verb in ("RSET", "NOOP"):
                self.reply("250 ok")
            elif verb == "QUIT":
                self.reply("221 bye")
                return
            else:
                self.reply("502 unknown")


class _Server(socketserver.ThreadingMixIn, socketserver.TCPServer):
    daemon_threads = True
    allow_reuse_address = True


class FakeSMTP:
    def __init__(self, port=0):
        self.mails, self.logins, self.lock = [], [], threading.Lock()
        self._server = _Server(("127.0.0.1", port), _Handler)
        self._server.box = self
        self.port = self._server.server_address[1]
        threading.Thread(target=self._server.serve_forever, daemon=True).start()

    def close(self):
        self._server.shutdown()
        self._server.server_close()

    def count(self, to=None):
        with self.lock:
            return len([m for m in self.mails if to is None or m["to"] == to.lower()])

    def last(self, to, timeout=10, after=0):
        """Le dernier mail vers to, parmi ceux reçus après les `after` premiers ; attend jusqu'à timeout."""
        end = time.time() + timeout
        while time.time() < end:
            with self.lock:
                mine = [m for m in self.mails if m["to"] == to.lower()]
            if len(mine) > after:
                return mine[-1]
            time.sleep(0.1)
        raise TimeoutError("no mail to " + to)

    def code_for(self, to, **kw):
        m = re.search(r"\b(\d{8})\b", self.last(to, **kw)["body"])
        return m.group(1) if m else None

    def reset_for(self, to, **kw):
        m = re.search(r"#reset=([0-9a-f]{64})", self.last(to, **kw)["body"])
        return m.group(1) if m else None
