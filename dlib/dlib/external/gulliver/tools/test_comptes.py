#!/usr/bin/env python3
# Le test des comptes de Gulliver (fork pour macOS, 2026-11-22) : mots de passe, sessions, administrateur.
#
#   python3 tools/test_comptes.py
#
# Sur un service d'essai isolé (port 18494, ses propres données dans build/comptes-test, hors ligne, sans modèle,
# effacées à la fin), jamais celui de l'utilisateur.  Vérifie : le premier mot de passe crée l'administrateur, une
# seule fois, jamais par la passerelle (X-Gulliver-Remote) ; les données exigent une session et ne s'ouvrent jamais
# pour un autre profil ; seul l'administrateur gère les profils, et jamais par la passerelle ; un mauvais mot de passe
# est refusé, puis freiné après 5 essais ; changer un mot de passe ferme les autres sessions ; la déconnexion ferme la
# session ; une base d'avant les comptes fait de son plus ancien profil l'administrateur, sans mot de passe.
import json
import os
import shutil
import signal
import ssl
import subprocess
import sys
import time
import urllib.error
import urllib.request

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../../../.."))
WORK = os.path.join(ROOT, "build", "comptes-test")
SERVICE = os.environ.get("GULLIVER_SERVICE", os.path.join(ROOT, "build", "macos-gulliver", "bin", "gulliver-service"))
PORT = 18494
BASE = f"https://127.0.0.1:{PORT}"
CTX = ssl.create_default_context()
CTX.check_hostname = False
CTX.verify_mode = ssl.CERT_NONE
failures = 0


def check(ok, what):
    global failures
    print(("ok   " if ok else "FAIL ") + what, flush=True)
    failures += 0 if ok else 1


def call(method, path, body=None, token=None, headers=None):
    """(statut, corps JSON)."""
    h = {"Content-Type": "application/json"}
    if token:
        h["X-Gulliver-Session"] = token
    h.update(headers or {})
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(BASE + path, data=data, method=method, headers=h)
    try:
        with urllib.request.urlopen(req, context=CTX, timeout=60) as r:
            return r.status, json.load(r)
    except urllib.error.HTTPError as e:
        try:
            return e.code, json.load(e)
        except ValueError:
            return e.code, {}


def code(body):
    return ((body or {}).get("error") or {}).get("code")


def start():
    log = open(os.path.join(WORK, "service.log"), "a")
    proc = subprocess.Popen([SERVICE, "--console"], env=dict(os.environ, GULLIVER_HOME=os.path.join(WORK, "home")),
                            stdout=log, stderr=log)
    for _ in range(60):
        try:
            if call("GET", "/gulliver/v1/status")[0] == 200:
                return proc
        except Exception:
            pass
        time.sleep(0.5)
    raise RuntimeError("the test service does not answer")


def stop(proc):
    proc.send_signal(signal.SIGTERM)
    try:
        proc.wait(30)
    except subprocess.TimeoutExpired:
        proc.kill()


def main():
    shutil.rmtree(WORK, ignore_errors=True)
    home = os.path.join(WORK, "home")
    os.makedirs(home, mode=0o700)
    with open(os.path.join(home, "settings.json"), "w") as f:
        json.dump({"port": PORT, "offline": True, "default_model": False, "lan": False}, f)
    proc = start()
    try:
        st, body = call("GET", "/gulliver/v1/profiles")
        check(st == 200 and body.get("setup") is True and body.get("profiles") == [], "sans profil : setup demandé")
        st, body = call("POST", "/gulliver/v1/setup", {"name": "Admin", "password": "mot-de-passe-1"},
                        headers={"X-Gulliver-Remote": "1"})
        check(st == 403 and code(body) == "setup_remote", "le premier mot de passe est refusé par la passerelle")
        st, body = call("POST", "/gulliver/v1/setup", {"name": "Admin", "password": "court"})
        check(st == 400, "un mot de passe de moins de 8 caractères est refusé")
        st, body = call("POST", "/gulliver/v1/setup", {"name": "Admin", "password": "mot-de-passe-1"})
        admin = body.get("token")
        admin_id = (body.get("profile") or {}).get("id")
        check(st == 200 and admin and len(admin) == 64 and body["profile"].get("admin") is True,
              "le premier mot de passe crée l'administrateur et ouvre sa session")
        check("password" not in json.dumps(body["profile"]).replace("has_password", ""),
              "l'empreinte du mot de passe ne sort jamais du service")
        st, body = call("POST", "/gulliver/v1/setup", {"name": "Autre", "password": "mot-de-passe-2"})
        check(st == 409 and code(body) == "setup_done", "le premier mot de passe ne se choisit qu'une fois")

        st, body = call("GET", "/gulliver/v1/profile/conversations", headers={"X-Gulliver-Profile": admin_id})
        check(st == 401 and code(body) == "session_required", "les données exigent une session (l'en-tête seul ne suffit plus)")
        st, body = call("GET", "/gulliver/v1/profile/conversations", token=admin)
        check(st == 200, "l'administrateur lit ses discussions")

        st, body = call("POST", "/gulliver/v1/profiles", {"name": "Lui", "password": "mot-de-passe-3"})
        check(st == 401, "créer un profil exige une session")
        st, body = call("POST", "/gulliver/v1/profiles", {"name": "Lui", "password": "mot-de-passe-3"}, token=admin,
                        headers={"X-Gulliver-Remote": "1"})
        check(st == 403 and code(body) == "admin_required", "la gestion des profils est refusée par la passerelle")
        st, body = call("POST", "/gulliver/v1/profiles", {"name": "Lui"}, token=admin)
        check(st == 400, "un profil se crée avec un mot de passe")
        st, body = call("POST", "/gulliver/v1/profiles", {"name": "Lui", "password": "mot-de-passe-3", "admin": True},
                        token=admin)
        lui_id = body.get("id")
        check(st == 200 and body.get("admin") is False, "l'administrateur crée un profil, jamais un second administrateur")

        st, body = call("POST", "/gulliver/v1/login", {"profile": lui_id, "password": "faux-mot-de-passe"})
        check(st == 401 and code(body) == "wrong_password", "un mauvais mot de passe est refusé")
        st, body = call("POST", "/gulliver/v1/login", {"name": "Lui", "password": "mot-de-passe-3"})
        lui = body.get("token")
        check(st == 200 and lui and body["profile"].get("admin") is False, "connexion par le nom et le mot de passe")
        st, body = call("GET", "/gulliver/v1/session", token=lui)
        check(st == 200 and body["profile"]["id"] == lui_id, "la session dit à qui elle appartient")

        st, body = call("PUT", f"/gulliver/v1/profile/conversations/c1",
                        {"doc": {"id": "c1", "title": "secret", "messages": []}}, token=admin)
        st2, body2 = call("GET", "/gulliver/v1/profile/conversations/c1", token=lui)
        st3, body3 = call("GET", "/gulliver/v1/profile/conversations", token=lui,
                          headers={"X-Gulliver-Profile": admin_id})
        check(st == 200 and st2 == 404 and st3 == 403 and code(body3) == "profile_mismatch",
              "un profil ne lit jamais les discussions d'un autre, même en nommant son identifiant")

        for path, method, b in (("/gulliver/v1/profiles", "POST", {"name": "X", "password": "mot-de-passe-4"}),
                                (f"/gulliver/v1/profiles/{admin_id}", "PUT", {"password": "mot-de-passe-5"}),
                                (f"/gulliver/v1/profiles/{admin_id}", "DELETE", None)):
            st, body = call(method, path, b, token=lui)
            check(st == 403 and code(body) == "admin_required", f"un profil simple ne gère pas les profils ({method})")

        st, body = call("DELETE", f"/gulliver/v1/profiles/{admin_id}", token=admin)
        check(st == 409 and code(body) == "admin_kept", "l'administrateur ne peut pas se supprimer")

        st, body = call("POST", "/gulliver/v1/login", {"name": "Lui", "password": "mot-de-passe-3"})
        lui2 = body.get("token")
        st, body = call("POST", "/gulliver/v1/session/password", {"current": "faux-faux-faux", "password": "nouveau-mdp-6"},
                        token=lui)
        check(st == 401, "changer son mot de passe exige l'ancien")
        st, body = call("POST", "/gulliver/v1/session/password", {"current": "mot-de-passe-3", "password": "nouveau-mdp-6"},
                        token=lui)
        check(st == 200 and call("GET", "/gulliver/v1/session", token=lui)[0] == 200
              and call("GET", "/gulliver/v1/session", token=lui2)[0] == 401,
              "changer son mot de passe garde sa session et ferme les autres")
        st, body = call("PUT", f"/gulliver/v1/profiles/{lui_id}", {"password": "par-l-admin-7", "name": "Lui bis"},
                        token=admin)
        check(st == 200 and body.get("name") == "Lui bis" and call("GET", "/gulliver/v1/session", token=lui)[0] == 401,
              "l'administrateur renomme et change le mot de passe ; les sessions du profil se ferment")
        st, body = call("POST", "/gulliver/v1/login", {"profile": lui_id, "password": "par-l-admin-7"})
        lui = body.get("token")
        check(st == 200, "connexion avec le mot de passe donné par l'administrateur")

        statuses = [call("POST", "/gulliver/v1/login", {"profile": lui_id, "password": "essai-faux-%d" % i})[0]
                    for i in range(6)]
        st, body = call("POST", "/gulliver/v1/login", {"profile": lui_id, "password": "par-l-admin-7"})
        check(statuses[:5] == [401] * 5 and statuses[5] == 429 and st == 429 and code(body) == "login_throttled",
              "après 5 mauvais mots de passe, les essais sont freinés (même le bon)")

        st, body = call("POST", "/gulliver/v1/logout", token=admin)
        check(st == 200 and call("GET", "/gulliver/v1/session", token=admin)[0] == 401, "la déconnexion ferme la session")
        st, body = call("POST", "/gulliver/v1/login", {"profile": admin_id, "password": "mot-de-passe-1"})
        admin = body.get("token")
        st, body = call("DELETE", f"/gulliver/v1/profiles/{lui_id}", token=admin)
        check(st == 200 and call("GET", "/gulliver/v1/session", token=lui)[0] == 401,
              "un profil supprimé emporte ses sessions")
        st, body = call("GET", "/gulliver/v1/session", token="0" * 64)
        check(st == 401, "un jeton inventé ne vaut rien")
    finally:
        stop(proc)

    # Une base d'avant les comptes : deux profils sans mot de passe ni rôle.
    db = os.path.join(home, "gulliver.db")
    subprocess.run(["sqlite3", db, "DELETE FROM profile_sessions; DELETE FROM profiles; "
                    "INSERT INTO profiles (id, name, created, last_used) VALUES ('aaa', 'Moi', 100, 100), "
                    "('bbb', 'Autre', 200, 300);"], check=True)
    proc = start()
    try:
        st, body = call("GET", "/gulliver/v1/profiles")
        roles = {p["name"]: (p["admin"], p["has_password"]) for p in body.get("profiles", [])}
        check(body.get("setup") is True and roles == {"Moi": (True, False), "Autre": (False, False)},
              "une base d'avant : le plus ancien profil devient l'administrateur, setup demandé")
        st, body = call("POST", "/gulliver/v1/login", {"name": "Autre", "password": "nimporte-quoi"})
        check(st == 403 and code(body) == "password_not_set", "un profil sans mot de passe ne s'ouvre pas")
        st, body = call("POST", "/gulliver/v1/setup", {"password": "mot-de-passe-8"})
        check(st == 200 and body["profile"]["name"] == "Moi" and body["profile"]["admin"] is True,
              "le premier mot de passe va à l'administrateur existant")
    finally:
        stop(proc)
        shutil.rmtree(WORK, ignore_errors=True)
    print("test_comptes: " + ("all checks passed" if failures == 0 else f"{failures} checks failed"))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
