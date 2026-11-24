#!/usr/bin/env python3
# Copyright (C) 2026  papy cyber
# License: Boost Software License   See LICENSE.txt for the full license.
"""Le test du surveillant (tools/surveillant.py), fork pour macOS, 2026-11-24.

    python3 tools/test_surveillant.py

Une demi-heure ramenée à 40 secondes, un relevé toutes les 2 secondes, et un seuil de processeur à 0 % : le Mac est
toujours « en souci ».  Le surveillant ne voit et ne peut arrêter que les processus du test (Watch(only=…)) : trois
`yes` relancés toutes les 6 secondes, comme une série d'essais dont chaque programme vit peu, et un `sleep` qui ne
calcule rien.  Le mail part vers un faux serveur d'envoi (faux_smtp.py), jamais vers le Bridge.  Contrôles : les
`yes` sont comptés et arrêtés, leur lanceur est nommé, le `sleep` est épargné, le mail le dit.  Environ une minute,
trois cœurs.
"""
import json
import os
import sqlite3
import subprocess
import sys
import tempfile
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from faux_smtp import FakeSMTP  # noqa: E402
import surveillant as sv  # noqa: E402


def main():
    failures = []

    def check(name, ok, detail=""):
        print(f"{'PASS' if ok else 'FAIL'}  {name}{('  ' + detail) if detail else ''}", flush=True)
        if not ok:
            failures.append(name)

    home = tempfile.mkdtemp(prefix="surveillant-")
    smtp = FakeSMTP()
    with open(os.path.join(home, "settings.json"), "w") as f:
        json.dump({"mail_from": "gulliver@essai.invalid", "mail_port": smtp.port}, f)
    with open(os.path.join(home, "mail.secret"), "w") as f:
        json.dump({"scope": "x", "secret": "s"}, f)
    db = sqlite3.connect(os.path.join(home, "gulliver.db"))
    db.execute("CREATE TABLE profiles (admin INTEGER, email TEXT)")
    db.execute("INSERT INTO profiles VALUES (1, 'admin@essai.invalid')")
    db.commit()
    db.close()

    sv.FENETRE, sv.PAS, sv.SEUIL_CPU, sv.COEURS = 40, 2, 0, 1.5
    mine, done = set(), threading.Event()

    def relaunch():
        while not done.is_set():
            ps = [subprocess.Popen(["yes"], stdout=subprocess.DEVNULL) for _ in range(3)]
            mine.update(p.pid for p in ps)
            for _ in range(60):
                if done.is_set() or any(p.poll() is not None for p in ps):
                    break
                time.sleep(0.1)
            for p in ps:
                if p.poll() is None:
                    p.kill()
                p.wait()

    worker = threading.Thread(target=relaunch, daemon=True)
    worker.start()
    idle = subprocess.Popen(["sleep", "300"])
    mine.add(idle.pid)
    try:
        w = sv.Watch(home, only=mine)
        for _ in range(45):
            w.tick()
            if smtp.count():
                break
            time.sleep(sv.PAS)
        done.set()
        worker.join()
        check("mail sent", smtp.count("admin@essai.invalid") == 1)
        body = smtp.last("admin@essai.invalid", timeout=1)["body"] if smtp.count() else ""
        check("the yes processes are stopped", "Arrêtés :" in body and "- yes (" in body, body.splitlines()[4:6].__str__())
        check("their launcher is named", "lancés par" in body)
        check("the idle process is spared", idle.poll() is None)
        check("the bridge password reached the server", smtp.logins == [("gulliver@essai.invalid", "s")])
    finally:
        done.set()
        idle.kill()
        smtp.close()
    print(f"\nfailed checks: {len(failures)}")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
