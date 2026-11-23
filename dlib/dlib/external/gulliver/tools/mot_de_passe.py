#!/usr/bin/env python3
# Copyright (C) 2026  papy cyber
# License: Boost Software License   See LICENSE.txt for the full license.
"""Remettre à neuf le mot de passe d'un profil de Gulliver, depuis le Mac seulement (fork pour macOS, 2026-11-23).

    python3 tools/mot_de_passe.py [--home DOSSIER] [--profil NOM]

Pour un mot de passe oublié : la page demande le mot de passe actuel, et seul l'administrateur change celui des
autres ; si c'est le sien qu'il a oublié, plus personne ne peut le changer par le service.  Ce programme écrit
directement dans la base du service (`gulliver.db` du dossier de données, GULLIVER_HOME par défaut) : il ne marche
donc que pour qui peut déjà lire et écrire les fichiers de Gulliver sur le Mac, jamais depuis le réseau.

Sans --profil, il prend l'administrateur.  Le nouveau mot de passe se tape deux fois, sans s'afficher ; quand l'entrée
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
import secrets
import sqlite3
import sys

ROUNDS = 600000          # comme host/accounts.cpp
PASSWORD_MIN, PASSWORD_MAX = 8, 256  # comme host/accounts.h, en octets UTF-8


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
    else:
        rows = db.execute("SELECT id, name FROM profiles WHERE admin = 1").fetchall()
    if len(rows) != 1:
        sys.exit("Profil introuvable." if not rows else "Plusieurs profils correspondent : préciser --profil.")
    pid, name = rows[0]
    password = read_password()
    if not PASSWORD_MIN <= len(password.encode("utf-8")) <= PASSWORD_MAX:
        sys.exit("Il faut de %d à %d caractères : rien n'est changé." % (PASSWORD_MIN, PASSWORD_MAX))
    with db:
        db.execute("UPDATE profiles SET password = ? WHERE id = ?", (hash_password(password), pid))
        closed = db.execute("DELETE FROM profile_sessions WHERE profile = ?", (pid,)).rowcount
    db.close()
    print("Mot de passe du profil « %s » changé ; %d session(s) fermée(s)." % (name, closed))


if __name__ == "__main__":
    main()
