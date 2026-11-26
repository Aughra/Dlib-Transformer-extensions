#!/usr/bin/env python3
# Copyright (C) 2026  papy cyber
# Le profil et la session des scénarios du test de la page (page_test.sh, comptes de Gulliver 6.31.0).
#
#   python3 session.py <url du service d'essai> <fichier du mot de passe de l'administrateur « banc »>
#
# Depuis le 2026-11-23, on se connecte par adresse mail, mot de passe et code reçu par mail : un faux facteur
# (tools/faux_smtp.py) garde les mails du service d'essai, dont l'adresse d'envoi et le port sont réglés ici.
# L'administrateur « banc » (banc@essai.invalid) choisit son mot de passe par le lien du premier passage (ou, pour un
# dossier d'essai d'avant les adresses, redonne son mot de passe avec son adresse), puis se connecte ; il supprime
# les profils « scenarios-* » d'avant (avec leurs données), en crée un neuf, qui choisit son mot de passe par le lien
# et s'y connecte.  Écrit « <identifiant du profil> <jeton de session> » sur la sortie.
import json, os, ssl, sys, urllib.request
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from faux_smtp import FakeSMTP  # noqa: E402
ctx = ssl.create_default_context(); ctx.check_hostname = False; ctx.verify_mode = ssl.CERT_NONE
url, password = sys.argv[1], open(sys.argv[2]).read().strip()
ADMIN = "banc@essai.invalid"
def call(method, path, body=None, token=""):
    req = urllib.request.Request(url + path, data=json.dumps(body).encode() if body is not None else None, method=method,
                                 headers={"Content-Type": "application/json", "X-Gulliver-Session": token})
    with urllib.request.urlopen(req, context=ctx, timeout=60) as r:
        return json.load(r)
def login(address, secret):
    before = smtp.count(address)
    challenge = call("POST", "/gulliver/v1/login", {"email": address, "password": secret})["challenge"]
    return call("POST", "/gulliver/v1/login/code", {"challenge": challenge, "code": smtp.code_for(address, after=before)})
smtp = FakeSMTP()
try:
    call("PUT", "/gulliver/v1/settings", {"mail_from": "gulliver@essai.invalid", "mail_port": smtp.port, "mail_link": ""})
    listing = call("GET", "/gulliver/v1/profiles")
    if listing.get("setup") and listing.get("setup_password"):
        # Un dossier d'essai d'avant les adresses : l'administrateur redonne son mot de passe avec son adresse.
        challenge = call("POST", "/gulliver/v1/setup", {"email": ADMIN, "password": password})["challenge"]
        admin = call("POST", "/gulliver/v1/login/code", {"challenge": challenge, "code": smtp.code_for(ADMIN)})["token"]
    elif listing.get("setup"):
        call("POST", "/gulliver/v1/setup", {"name": "banc", "email": ADMIN})
        admin = call("POST", "/gulliver/v1/password/reset", {"token": smtp.reset_for(ADMIN), "password": password})["token"]
    else:
        admin = login(ADMIN, password)["token"]
    for p in call("GET", "/gulliver/v1/profiles", token=admin).get("profiles", []):
        if p["name"].startswith("scenarios-"):
            call("DELETE", "/gulliver/v1/profiles/" + p["id"], token=admin)
    run = os.urandom(4).hex()
    address = "scenarios-" + run + "@essai.invalid"
    p = call("POST", "/gulliver/v1/profiles", {"name": "scenarios-" + run, "email": address}, token=admin)
    call("POST", "/gulliver/v1/password/forgot", {"email": address})
    s = call("POST", "/gulliver/v1/password/reset", {"token": smtp.reset_for(address),
                                                        "password": "Scenario-1-" + password + run})
    print(p["id"], s["token"])
finally:
    smtp.close()
