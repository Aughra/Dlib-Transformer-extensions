#!/usr/bin/env python3
# Copyright (C) 2026  papy cyber
# License: Boost Software License   See LICENSE.txt for the full license.
"""Remettre à neuf le mot de passe d'un profil de Gulliver, depuis le Mac seulement (fork pour macOS, 2026-11-23).

    python3 tools/mot_de_passe.py [--home DOSSIER] [--profil NOM | --adresse MAIL] [--email NOUVELLE]

Pour un mot de passe oublié : la page demande le mot de passe actuel, et seul l'administrateur change celui des
autres ; si c'est le sien qu'il a oublié, plus personne ne peut le changer par le service.  Ce programme écrit
directement dans la base du service (`gulliver.db` du dossier de données, GULLIVER_HOME par défaut) : il ne marche
donc que pour qui peut déjà lire et écrire les fichiers de Gulliver sur le Mac, jamais depuis le réseau.

Sans --profil ni --adresse, il prend l'administrateur.  --email donne au profil sa nouvelle adresse de connexion
(2026-11-23 : on se connecte par son adresse mail, et une adresse mal tapée par l'administrateur se corrige ici) ;
avec --email, le mot de passe n'est changé que si --mot-de-passe est aussi donné.  Le nouveau mot de passe se tape deux fois, sans s'afficher ; quand l'entrée
n'est pas un terminal, il est lu sur la première ligne de l'entrée (jamais en argument : il resterait dans
l'historique et dans la liste des processus).  L'empreinte suit celle du service (`host/accounts.cpp`) :
« pbkdf2-sha256$600000$<sel de 16 octets>$<clé de 32 octets> », en hexadécimal.  Toutes les sessions du profil sont
fermées : ses appareils redemandent le mot de passe.  Le service peut tourner pendant ce temps (base en WAL ; il relit
les profils à chaque demande).
"""
import argparse
import getpass
import hashlib
import os
import re
import secrets
import sqlite3
import sys

ROUNDS = 600000          # comme host/accounts.cpp
PASSWORD_MIN, PASSWORD_MAX = 20, 2048  # comme host/accounts.h, en octets UTF-8 (règle du fork, 2026-11-26)
# Une adresse simple, comme valid_mail_address de host/mailer.cpp (en plus strict sur les caractères).
EMAIL = re.compile(r"[^\s@<>\"(),;:\\\[\]]+@[^\s@<>\"(),;:\\\[\].]+(\.[^\s@<>\"(),;:\\\[\].]+)+")


def password_valid(password):
    """Même règle que host/accounts.cpp::password_valid : rend "" ou le code court de l'erreur."""
    if len(password.encode("utf-8")) < PASSWORD_MIN:
        return "password_too_short"
    if len(password.encode("utf-8")) > PASSWORD_MAX:
        return "password_too_long"
    has_lower = any("a" <= c <= "z" for c in password)
    has_upper = any("A" <= c <= "Z" for c in password)
    has_digit = any("0" <= c <= "9" for c in password)
    # Tout caractère non alphanumérique qui n'est pas un espace ASCII compte comme spécial.
    has_special = any(not (c.isalnum() or c.isspace()) for c in password)
    if not has_lower:
        return "password_needs_lowercase"
    if not has_upper:
        return "password_needs_uppercase"
    if not has_digit:
        return "password_needs_digit"
    if not has_special:
        return "password_needs_special"
    return ""


def hash_password(password):
    salt = secrets.token_bytes(16)
    key = hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"), salt, ROUNDS, 32)
    return "pbkdf2-sha256$%d$%s$%s" % (ROUNDS, salt.hex(), key.hex())


def read_password():
    if sys.stdin.isatty():
        first = getpass.getpass("Nouveau mot de passe : ")
        if getpass.getpass("Encore une fois : ") != first:
            sys.exit("Les deux mots de passe diffèrent : rien n'est changé.")
        return first
    return sys.stdin.readline().rstrip("\r\n")


def main():
    parser = argparse.ArgumentParser(description="Remettre à neuf le mot de passe d'un profil de Gulliver.")
    parser.add_argument("--home", default=os.environ.get("GULLIVER_HOME"),
                        help="dossier de données du service (GULLIVER_HOME par défaut)")
    parser.add_argument("--profil", help="nom du profil (l'administrateur par défaut)")
    parser.add_argument("--adresse", help="adresse mail du profil (au lieu de son nom)")
    parser.add_argument("--email", help="la nouvelle adresse mail de connexion du profil")
    parser.add_argument("--mot-de-passe", dest="password", action="store_true",
                        help="avec --email : changer aussi le mot de passe")
    args = parser.parse_args()
    if not args.home:
        sys.exit("Dossier de données inconnu : donner --home ou GULLIVER_HOME.")
    path = os.path.join(args.home, "gulliver.db")
    if not os.path.isfile(path):
        sys.exit("Pas de base du service dans " + args.home + ".")
    db = sqlite3.connect(path, timeout=10)
    db.execute("PRAGMA foreign_keys=ON")
    if args.profil:
        rows = db.execute("SELECT id, name FROM profiles WHERE name = ?", (args.profil,)).fetchall()
    elif args.adresse:
        rows = db.execute("SELECT id, name FROM profiles WHERE email = ?", (args.adresse.strip().lower(),)).fetchall()
    else:
        rows = db.execute("SELECT id, name FROM profiles WHERE admin = 1").fetchall()
    if len(rows) != 1:
        sys.exit("Profil introuvable." if not rows else "Plusieurs profils correspondent : préciser --profil.")
    pid, name = rows[0]
    if args.email is not None:
        email = args.email.strip().lower()
        if not EMAIL.fullmatch(email):
            sys.exit("Adresse mal formée : rien n'est changé.")
        if db.execute("SELECT 1 FROM profiles WHERE email = ? AND id <> ?", (email, pid)).fetchone():
            sys.exit("Un autre profil a déjà cette adresse : rien n'est changé.")
        with db:
            db.execute("UPDATE profiles SET email = ? WHERE id = ?", (email, pid))
        print("Adresse du profil « %s » : %s." % (name, email))
        if not args.password:
            db.close()
            return
    password = read_password()
    error = password_valid(password)
    if error:
        messages = {
            "password_too_short": "Il faut au moins %d caractères : rien n'est changé." % PASSWORD_MIN,
            "password_too_long": "Il ne faut pas plus de %d caractères : rien n'est changé." % PASSWORD_MAX,
            "password_needs_lowercase": "Il faut au moins une minuscule (a-z) : rien n'est changé.",
            "password_needs_uppercase": "Il faut au moins une majuscule (A-Z) : rien n'est changé.",
            "password_needs_digit": "Il faut au moins un chiffre (0-9) : rien n'est changé.",
            "password_needs_special": "Il faut au moins un caractère spécial : rien n'est changé.",
        }
        sys.exit(messages.get(error, "Mot de passe refusé : rien n'est changé."))
    with db:
        db.execute("UPDATE profiles SET password = ? WHERE id = ?", (hash_password(password), pid))
        closed = db.execute("DELETE FROM profile_sessions WHERE profile = ?", (pid,)).rowcount
    db.close()
    print("Mot de passe du profil « %s » changé ; %d session(s) fermée(s)." % (name, closed))


if __name__ == "__main__":
    main()
