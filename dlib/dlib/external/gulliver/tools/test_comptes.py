#!/usr/bin/env python3
# Le test des comptes de Gulliver (fork pour macOS, 2026-11-22) : adresses mail, mots de passe, codes, sessions,
# administrateur.
#
#   python3 tools/test_comptes.py
#
# Sur un service d'essai isolé (port 18494, ses propres données dans build/comptes-test, hors ligne, sans modèle,
# effacées à la fin), jamais celui de l'utilisateur, et un faux facteur (tools/faux_smtp.py) qui garde les mails au
# lieu de les envoyer.  Vérifie : le premier compte crée l'administrateur, une seule fois, jamais par la passerelle
# (X-Gulliver-Remote), et lui envoie le lien pour choisir son mot de passe ; un lien ne sert qu'une fois ; la connexion
# se fait par l'adresse et le mot de passe, puis par le code à 8 chiffres reçu par mail (5 essais, une seule fois) ;
# une adresse inconnue répond comme un mauvais mot de passe, et « mot de passe oublié » comme une adresse connue ; le
# frein des mails ; le mot de passe du facteur reste masqué et rangé à part ; les données exigent une session et ne
# s'ouvrent jamais pour un autre profil ; seul l'administrateur gère les profils (nom et adresse, jamais le mot de
# passe des autres), et jamais par la passerelle ; un mauvais mot de passe est freiné après 5 essais ; changer un mot
# de passe ferme les autres sessions ; une base d'avant les adresses fait vérifier celle de l'administrateur par un
# code ; les actions qui changent le service restent libres depuis le Mac, et depuis le réseau de la maison (port
# 18496, appelé par l'adresse du Mac sur ce réseau) sont réservées à la session de l'administrateur.
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

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from faux_smtp import FakeSMTP  # noqa: E402

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../../../.."))
WORK = os.path.join(ROOT, "build", "comptes-test")
SERVICE = os.environ.get("GULLIVER_SERVICE", os.path.join(ROOT, "build", "macos-gulliver", "bin", "gulliver-service"))
PORT = 18494
LAN_PORT = 18496
BASE = f"https://127.0.0.1:{PORT}"
CTX = ssl.create_default_context()
CTX.check_hostname = False
CTX.verify_mode = ssl.CERT_NONE
failures = 0


def check(ok, what):
    global failures
    print(("ok   " if ok else "FAIL ") + what, flush=True)
    failures += 0 if ok else 1


def lan_address():
    """L'adresse du Mac sur le réseau de la maison (interface de la route par défaut), ou None."""
    try:
        route = subprocess.run(["route", "-n", "get", "default"], capture_output=True, text=True).stdout
        iface = next(l.split()[-1] for l in route.splitlines() if "interface:" in l)
        return subprocess.run(["ipconfig", "getifaddr", iface], capture_output=True, text=True).stdout.strip() or None
    except Exception:
        return None


def call(method, path, body=None, token=None, headers=None, base=None):
    """(statut, corps JSON)."""
    h = {"Content-Type": "application/json"}
    if token:
        h["X-Gulliver-Session"] = token
    h.update(headers or {})
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request((base or BASE) + path, data=data, method=method, headers=h)
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


def login(smtp, address, password):
    """Une session par l'adresse, le mot de passe et le code du mail : (statut, corps de /login/code)."""
    before = smtp.count(address)
    st, body = call("POST", "/gulliver/v1/login", {"email": address, "password": password})
    if st != 200:
        return st, body
    return call("POST", "/gulliver/v1/login/code",
                {"challenge": body["challenge"], "code": smtp.code_for(address, after=before)})


def choose_password(smtp, address, password):
    """Le mot de passe choisi par le lien du dernier mail vers address : (statut, corps de /password/reset)."""
    return call("POST", "/gulliver/v1/password/reset", {"token": smtp.reset_for(address), "password": password})


def main():
    shutil.rmtree(WORK, ignore_errors=True)
    home = os.path.join(WORK, "home")
    os.makedirs(home, mode=0o700)
    smtp = FakeSMTP()
    with open(os.path.join(home, "settings.json"), "w") as f:
        json.dump({"port": PORT, "offline": True, "default_model": False, "lan": True, "lan_port": LAN_PORT,
                   "mail_from": "gulliver@essai.fr", "mail_port": smtp.port}, f)
    proc = start()
    ADMIN, LUI = "admin@essai.fr", "lui@essai.fr"
    try:
        st, body = call("GET", "/gulliver/v1/profiles")
        check(st == 200 and body.get("setup") is True and body.get("profiles") == [] and body.get("mail") is True,
              "sans profil : premier compte demandé, facteur réglé")
        st, body = call("POST", "/gulliver/v1/setup", {"name": "Admin", "email": ADMIN}, headers={"X-Gulliver-Remote": "1"})
        check(st == 403 and code(body) == "setup_remote", "le premier compte est refusé par la passerelle")
        st, body = call("POST", "/gulliver/v1/setup", {"name": "Admin", "email": "pas-une-adresse"})
        check(st == 400 and code(body) == "email", "une adresse mal formée est refusée")
        st, body = call("POST", "/gulliver/v1/setup", {"name": "Admin", "email": " Admin@Essai.FR "})
        mail = smtp.last(ADMIN)
        check(st == 200 and body.get("sent") is True and "token" not in body and mail["subject"].startswith("Choisir")
              and mail["from"] == "gulliver@essai.fr" and smtp.reset_for(ADMIN),
              "le premier compte crée l'administrateur sans mot de passe et lui envoie le lien (adresse en minuscules)")
        st, body = call("POST", "/gulliver/v1/setup", {"name": "Autre", "email": "autre@essai.fr"})
        check(st == 409 and code(body) == "setup_done", "le premier compte ne se crée qu'une fois")
        st, body = call("POST", "/gulliver/v1/login", {"email": ADMIN, "password": "nimporte-quoi"})
        check(st == 403 and code(body) == "password_not_set", "sans mot de passe, la connexion renvoie au lien")
        st, body = call("POST", "/gulliver/v1/password/reset", {"token": "0" * 64, "password": "mot-de-passe-1"})
        check(st == 401 and code(body) == "link_expired", "un lien inventé ne vaut rien")
        token = smtp.reset_for(ADMIN)
        st, body = call("POST", "/gulliver/v1/password/reset", {"token": token, "password": "court"})
        check(st == 400, "un mot de passe de moins de 8 caractères est refusé")
        st, body = call("POST", "/gulliver/v1/password/reset", {"token": token, "password": "mot-de-passe-1"})
        admin = body.get("token")
        admin_id = (body.get("profile") or {}).get("id")
        check(st == 200 and admin and len(admin) == 64 and body["profile"].get("admin") is True
              and body["profile"].get("email") == ADMIN, "le lien choisit le mot de passe et ouvre la session")
        check("pbkdf2" not in json.dumps(body), "l'empreinte du mot de passe ne sort jamais du service")
        st, body = call("POST", "/gulliver/v1/password/reset", {"token": token, "password": "mot-de-passe-9"})
        check(st == 401 and code(body) == "link_expired", "un lien ne sert qu'une fois")

        st, body = call("POST", "/gulliver/v1/login", {"email": ADMIN, "password": "faux-mot-de-passe"})
        st2, body2 = call("POST", "/gulliver/v1/login", {"email": "inconnu@essai.fr", "password": "mot-de-passe-1"})
        check(st == 401 and code(body) == "wrong_password" and st2 == 401 and code(body2) == "wrong_password",
              "un mauvais mot de passe et une adresse inconnue reçoivent la même réponse")
        before = smtp.count(ADMIN)
        st, body = call("POST", "/gulliver/v1/login", {"email": ADMIN, "password": "mot-de-passe-1"})
        challenge = body.get("challenge")
        the_code = smtp.code_for(ADMIN, after=before)
        check(st == 200 and challenge and len(challenge) == 64 and "token" not in body and the_code,
              "un bon mot de passe envoie un code à 8 chiffres, sans ouvrir de session")
        wrong = "%08d" % ((int(the_code) + 1) % 100000000)
        st, body = call("POST", "/gulliver/v1/login/code", {"challenge": challenge, "code": wrong})
        check(st == 401 and code(body) == "wrong_code" and body["error"].get("left") == 4, "un mauvais code est refusé")
        st, body = call("POST", "/gulliver/v1/login/code", {"challenge": challenge, "code": the_code[:4] + " " + the_code[4:]})
        check(st == 200 and len(body.get("token", "")) == 64, "le bon code ouvre la session (espaces permis)")
        st, body = call("POST", "/gulliver/v1/login/code", {"challenge": challenge, "code": the_code})
        check(st == 401 and code(body) == "code_expired", "un code ne sert qu'une fois")
        before = smtp.count(ADMIN)
        st, body = call("POST", "/gulliver/v1/login", {"email": ADMIN, "password": "mot-de-passe-1"})
        challenge, the_code = body.get("challenge"), smtp.code_for(ADMIN, after=before)
        wrong = "%08d" % ((int(the_code) + 1) % 100000000)
        replies = [call("POST", "/gulliver/v1/login/code", {"challenge": challenge, "code": wrong})[1] for _ in range(5)]
        st, body = call("POST", "/gulliver/v1/login/code", {"challenge": challenge, "code": the_code})
        check([code(r) for r in replies] == ["wrong_code"] * 4 + ["code_expired"] and st == 401
              and code(body) == "code_expired", "après 5 mauvais codes, même le bon ne vaut plus rien")

        # Le mot de passe du facteur : reçu par le serveur, masqué dans les réglages, rangé à part (0600).
        st, body = call("PUT", "/gulliver/v1/settings", {"mail_secret": "secret-du-bridge"})
        settings = call("GET", "/gulliver/v1/settings")[1]
        secret_file = os.path.join(home, "mail.secret")
        with open(os.path.join(home, "settings.json")) as f:
            on_disk = f.read()
        st2, _ = login(smtp, ADMIN, "mot-de-passe-1")
        check(st == 200 and settings.get("mail_secret") == "********" and "secret-du-bridge" not in on_disk
              and (os.stat(secret_file).st_mode & 0o077) == 0 and st2 == 200
              and smtp.logins and smtp.logins[-1] == ("gulliver@essai.fr", "secret-du-bridge"),
              "le mot de passe du facteur sert à l'envoi, reste masqué et rangé à part")
        st, body = call("PUT", "/gulliver/v1/settings", {"mail_link": "javascript:alert(1)"})
        st2, body2 = call("PUT", "/gulliver/v1/settings", {"mail_from": "pas une adresse"})
        check(st == 400 and st2 == 400, "une adresse de page ou d'envoi mal formée est refusée")
        call("PUT", "/gulliver/v1/settings", {"mail_link": "https://gulliver.example.org"})

        st, body = call("GET", "/gulliver/v1/profiles")
        check(st == 200 and body.get("profiles") == [], "sans session, la liste des profils ne montre rien")
        st, body = call("GET", "/gulliver/v1/profile/conversations", headers={"X-Gulliver-Profile": admin_id})
        check(st == 401 and code(body) == "session_required", "les données exigent une session (l'en-tête seul ne suffit plus)")
        st, body = call("GET", "/gulliver/v1/profile/conversations", token=admin)
        check(st == 200, "l'administrateur lit ses discussions")

        st, body = call("POST", "/gulliver/v1/profiles", {"name": "Lui", "email": LUI})
        check(st == 401, "créer un profil exige une session")
        st, body = call("POST", "/gulliver/v1/profiles", {"name": "Lui", "email": LUI}, token=admin,
                        headers={"X-Gulliver-Remote": "1"})
        check(st == 403 and code(body) == "admin_required", "la gestion des profils est refusée par la passerelle")
        st, body = call("POST", "/gulliver/v1/profiles", {"name": "Lui"}, token=admin)
        check(st == 400 and code(body) == "email", "un profil se crée avec une adresse")
        st, body = call("POST", "/gulliver/v1/profiles", {"name": "Lui", "email": ADMIN}, token=admin)
        check(st == 409 and code(body) == "email_taken", "deux profils n'ont jamais la même adresse")
        st, body = call("POST", "/gulliver/v1/profiles", {"name": "Lui", "email": LUI, "admin": True}, token=admin)
        lui_id = body.get("id")
        check(st == 200 and body.get("admin") is False and body.get("has_password") is False,
              "l'administrateur crée un profil sans mot de passe, jamais un second administrateur")
        listing = call("GET", "/gulliver/v1/profiles", token=admin)[1].get("profiles", [])
        check({p["name"]: p.get("email") for p in listing} == {"Admin": ADMIN, "Lui": LUI},
              "l'administrateur voit les adresses des profils")

        st, body = call("POST", "/gulliver/v1/password/forgot", {"email": "personne@essai.fr"})
        check(st == 200 and body.get("sent") is True and smtp.count("personne@essai.fr") == 0,
              "« mot de passe oublié » répond de même pour une adresse inconnue, sans mail")
        st, body = call("POST", "/gulliver/v1/password/forgot", {"email": LUI}, headers={"X-Gulliver-Remote": "1"})
        link = smtp.last(LUI)["body"]
        check(st == 200 and "https://gulliver.example.org/#reset=" in link,
              "« mot de passe oublié » marche par la passerelle, et le lien suit le réglage mail_link")
        st, body = call("POST", "/gulliver/v1/password/forgot", {"email": LUI})
        check(st == 429 and code(body) == "mail_throttled" and smtp.count(LUI) == 1,
              "un seul mail par minute vers une même adresse")
        st, body = choose_password(smtp, LUI, "mot-de-passe-3")
        lui = body.get("token")
        check(st == 200 and lui and body["profile"].get("admin") is False, "le premier mot de passe d'un profil, par le lien")
        st, body = call("GET", "/gulliver/v1/session", token=lui)
        check(st == 200 and body["profile"]["id"] == lui_id, "la session dit à qui elle appartient")
        listing = call("GET", "/gulliver/v1/profiles", token=lui)[1].get("profiles", [])
        check({p["name"]: p.get("email") for p in listing} == {"Admin": None, "Lui": LUI},
              "un profil simple ne voit que sa propre adresse")

        st, body = call("PUT", "/gulliver/v1/profile/conversations/c1",
                        {"doc": {"id": "c1", "title": "secret", "messages": []}}, token=admin)
        st2, body2 = call("GET", "/gulliver/v1/profile/conversations/c1", token=lui)
        st3, body3 = call("GET", "/gulliver/v1/profile/conversations", token=lui,
                          headers={"X-Gulliver-Profile": admin_id})
        check(st == 200 and st2 == 404 and st3 == 403 and code(body3) == "profile_mismatch",
              "un profil ne lit jamais les discussions d'un autre, même en nommant son identifiant")

        for path, method, b in (("/gulliver/v1/profiles", "POST", {"name": "X", "email": "x@essai.fr"}),
                                (f"/gulliver/v1/profiles/{admin_id}", "PUT", {"email": "pirate@essai.fr"}),
                                (f"/gulliver/v1/profiles/{admin_id}", "DELETE", None)):
            st, body = call(method, path, b, token=lui)
            check(st == 403 and code(body) == "admin_required", f"un profil simple ne gère pas les profils ({method})")
        st, body = call("PUT", f"/gulliver/v1/profiles/{lui_id}", {"password": "par-l-admin-7"}, token=admin)
        check(st == 400 and code(body) == "password_by_mail", "l'administrateur ne choisit plus le mot de passe des autres")

        st, body = call("DELETE", f"/gulliver/v1/profiles/{admin_id}", token=admin)
        check(st == 409 and code(body) == "admin_kept", "l'administrateur ne peut pas se supprimer")

        # Les actions qui changent le service : le journal est la plus inoffensive à appeler.
        st, body = call("GET", "/gulliver/v1/logs")
        check(st == 200, "depuis le Mac, le journal du service se lit sans session (agents et outils du Mac)")
        st, body = call("GET", "/gulliver/v1/logs", token=admin, headers={"X-Gulliver-Remote": "1"})
        check(st == 403 and code(body) == "admin_required", "par la passerelle, le journal est refusé, même à l'administrateur")
        ip = lan_address()
        if not ip:
            check(False, "l'adresse du Mac sur le réseau de la maison est introuvable")
        else:
            lan = f"http://{ip}:{LAN_PORT}"
            st, body = call("GET", "/gulliver/v1/logs", base=lan)
            check(st == 401 and code(body) == "session_required", "depuis le réseau de la maison, il faut une session")
            st, body = call("PUT", "/gulliver/v1/settings", {"lan": False}, token=lui, base=lan)
            check(st == 403 and code(body) == "admin_required",
                  "depuis le réseau de la maison, un profil simple ne change pas les réglages")
            st, body = call("POST", "/gulliver/v1/models/refresh", token=lui, base=lan)
            check(st == 403 and code(body) == "admin_required", "ni les modèles")
            st, body = call("GET", "/gulliver/v1/logs", token=admin, base=lan)
            check(st == 200, "depuis le réseau de la maison, l'administrateur lit le journal")
            st, body = call("GET", "/gulliver/v1/settings", token=lui, base=lan)
            check(st == 200, "les réglages restent lisibles par chacun")
            st, body = call("GET", "/gulliver/v1/logs", base=f"http://127.0.0.1:{LAN_PORT}")
            check(st == 200, "par la porte du réseau appelée depuis le Mac lui-même, pas de session demandée")

        st, body = login(smtp, LUI, "mot-de-passe-3")
        lui2 = body.get("token")
        st, body = call("POST", "/gulliver/v1/session/password", {"current": "faux-faux-faux", "password": "nouveau-mdp-6"},
                        token=lui)
        check(st == 401, "changer son mot de passe exige l'ancien")
        st, body = call("POST", "/gulliver/v1/session/password", {"current": "mot-de-passe-3", "password": "nouveau-mdp-6"},
                        token=lui)
        check(st == 200 and lui2 and call("GET", "/gulliver/v1/session", token=lui)[0] == 200
              and call("GET", "/gulliver/v1/session", token=lui2)[0] == 401,
              "changer son mot de passe garde sa session et ferme les autres")
        st, body = call("PUT", f"/gulliver/v1/profiles/{lui_id}", {"name": "Lui bis", "email": "lui.bis@essai.fr"},
                        token=admin)
        check(st == 200 and body.get("name") == "Lui bis" and body.get("email") == "lui.bis@essai.fr"
              and login(smtp, "lui.bis@essai.fr", "nouveau-mdp-6")[0] == 200,
              "l'administrateur renomme et change l'adresse ; la connexion suit la nouvelle adresse")

        statuses = [call("POST", "/gulliver/v1/login", {"email": "lui.bis@essai.fr", "password": "essai-faux-%d" % i})[0]
                    for i in range(6)]
        st, body = call("POST", "/gulliver/v1/login", {"email": "lui.bis@essai.fr", "password": "nouveau-mdp-6"})
        check(statuses[:5] == [401] * 5 and statuses[5] == 429 and st == 429 and code(body) == "login_throttled",
              "après 5 mauvais mots de passe, les essais sont freinés (même le bon)")

        # Le mot de passe oublié de l'administrateur, remis à neuf depuis le Mac (tools/mot_de_passe.py), service lancé.
        tool = os.path.join(os.path.dirname(os.path.abspath(__file__)), "mot_de_passe.py")
        out = subprocess.run([sys.executable, tool, "--home", home], input="oublie-remis-8\n", capture_output=True,
                             text=True)
        st, body = login(smtp, ADMIN, "oublie-remis-8")
        check(out.returncode == 0 and st == 200 and call("GET", "/gulliver/v1/session", token=admin)[0] == 401
              and call("POST", "/gulliver/v1/login", {"email": ADMIN, "password": "mot-de-passe-1"})[0] == 401,
              "mot_de_passe.py remet à neuf le mot de passe de l'administrateur et ferme ses sessions")
        out = subprocess.run([sys.executable, tool, "--home", home], input="mot-de-passe-1\n", capture_output=True,
                             text=True)
        admin = body.get("token")
        check(out.returncode == 0 and call("GET", "/gulliver/v1/session", token=admin)[0] == 401
              and subprocess.run([sys.executable, tool, "--home", home], input="court\n", capture_output=True, text=True).returncode != 0,
              "mot_de_passe.py remet l'ancien, et refuse un mot de passe trop court")

        st, body = login(smtp, ADMIN, "mot-de-passe-1")
        admin = body.get("token")
        st, body = call("POST", "/gulliver/v1/logout", token=admin)
        check(st == 200 and call("GET", "/gulliver/v1/session", token=admin)[0] == 401, "la déconnexion ferme la session")
        st, body = login(smtp, ADMIN, "mot-de-passe-1")
        admin = body.get("token")
        st, body = call("DELETE", f"/gulliver/v1/profiles/{lui_id}", token=admin)
        check(st == 200 and call("GET", "/gulliver/v1/session", token=lui)[0] == 401,
              "un profil supprimé emporte ses sessions")
        st, body = call("GET", "/gulliver/v1/session", token="0" * 64)
        check(st == 401, "un jeton inventé ne vaut rien")

        # Le Bridge arrêté : un port où personne n'écoute.
        call("PUT", "/gulliver/v1/settings", {"mail_port": 1})
        st, body = call("POST", "/gulliver/v1/login", {"email": ADMIN, "password": "mot-de-passe-1"})
        check(st == 503 and code(body) == "mail_failed", "sans facteur qui réponde, la connexion le dit (mail_failed)")
        call("PUT", "/gulliver/v1/settings", {"mail_port": smtp.port})
    finally:
        stop(proc)

    # Une base d'avant les adresses : deux profils sans adresse, l'administrateur avec son mot de passe.
    db = os.path.join(home, "gulliver.db")
    subprocess.run(["sqlite3", db, "DELETE FROM profile_sessions; DELETE FROM profiles; "
                    "INSERT INTO profiles (id, name, created, last_used) VALUES ('aaa', 'Moi', 100, 100), "
                    "('bbb', 'Autre', 200, 300);"], check=True)
    proc = start()
    try:
        st, body = call("GET", "/gulliver/v1/profiles")
        check(body.get("setup") is True and body.get("setup_password") is False,
              "une base d'avant : le plus ancien profil devient l'administrateur, premier compte demandé")
        call("PUT", "/gulliver/v1/settings", {"mail_from": "gulliver@essai.fr"})  # sans effet : déjà réglé
        stop(proc)
        out = subprocess.run([sys.executable, tool, "--home", home, "--profil", "Moi"], input="ancien-mdp-10\n",
                             capture_output=True, text=True)
        proc = start()
        st, body = call("GET", "/gulliver/v1/profiles")
        check(out.returncode == 0 and body.get("setup") is True and body.get("setup_password") is True,
              "un administrateur d'avant les adresses, avec mot de passe : il le redonne avec son adresse")
        st, body = call("POST", "/gulliver/v1/setup", {"email": "moi@essai.fr", "password": "pas-le-bon-mdp"})
        check(st == 401 and code(body) == "wrong_password", "sans son mot de passe, pas d'adresse")
        st, body = call("POST", "/gulliver/v1/setup", {"email": "moi@essai.fr", "password": "ancien-mdp-10"})
        challenge = body.get("challenge")
        still = call("GET", "/gulliver/v1/profiles")[1].get("setup")
        st2, body2 = call("POST", "/gulliver/v1/login/code",
                          {"challenge": challenge, "code": smtp.code_for("moi@essai.fr")})
        check(st == 200 and challenge and still is True and st2 == 200 and body2["profile"].get("email") == "moi@essai.fr"
              and call("GET", "/gulliver/v1/profiles")[1].get("setup") is False,
              "l'adresse de l'administrateur n'est rangée qu'une fois vérifiée par le code")
        st, body = call("POST", "/gulliver/v1/login", {"email": "autre@essai.fr", "password": "nimporte-quoi"})
        check(st == 401, "un profil sans adresse ne s'ouvre pas")
    finally:
        stop(proc)
        smtp.close()
        shutil.rmtree(WORK, ignore_errors=True)
    print("test_comptes: " + ("all checks passed" if failures == 0 else f"{failures} checks failed"))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
