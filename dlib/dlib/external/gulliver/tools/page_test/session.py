#!/usr/bin/env python3
# Copyright (C) 2026  papy cyber
# Le profil et la session des scénarios du test de la page (page_test.sh, comptes de Gulliver 6.31.0).
#
#   python3 session.py <url du service d'essai> <fichier du mot de passe de l'administrateur « banc »>
#
# L'administrateur « banc » du service d'essai choisit son mot de passe au premier passage (setup), puis se connecte ;
# il supprime les profils « scenarios-* » d'avant (avec leurs données), en crée un neuf et s'y connecte.  Écrit
# « <identifiant du profil> <jeton de session> » sur la sortie.
import json, os, ssl, sys, urllib.request
ctx = ssl.create_default_context(); ctx.check_hostname = False; ctx.verify_mode = ssl.CERT_NONE
url, password = sys.argv[1], open(sys.argv[2]).read().strip()
def call(method, path, body=None, token=""):
    req = urllib.request.Request(url + path, data=json.dumps(body).encode() if body is not None else None, method=method,
                                 headers={"Content-Type": "application/json", "X-Gulliver-Session": token})
    with urllib.request.urlopen(req, context=ctx, timeout=60) as r:
        return json.load(r)
listing = call("GET", "/gulliver/v1/profiles")
# Une base d'avant les comptes a fait de son plus ancien profil l'administrateur : on se connecte sous son nom.
name = next((p["name"] for p in listing.get("profiles", []) if p.get("admin")), "banc")
admin = (call("POST", "/gulliver/v1/setup", {"name": name, "password": password}) if listing.get("setup")
         else call("POST", "/gulliver/v1/login", {"name": name, "password": password}))["token"]
for p in listing.get("profiles", []):
    if p["name"].startswith("scenarios-"):
        call("DELETE", "/gulliver/v1/profiles/" + p["id"], token=admin)
run = os.urandom(4).hex()
p = call("POST", "/gulliver/v1/profiles", {"name": "scenarios-" + run, "password": password + run}, token=admin)
s = call("POST", "/gulliver/v1/login", {"profile": p["id"], "password": password + run})
print(p["id"], s["token"])
