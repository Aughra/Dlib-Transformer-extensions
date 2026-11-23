#!/usr/bin/env python3
# Copyright (C) 2026  papy cyber
# License: Boost Software License   See LICENSE.txt for the full license.
"""Le surveillant du Mac (fork pour macOS, 2026-11-23, demande de l'humain : « un script qui tourne en tâche de fond,
qui surveille l'utilisation du processeur et la température, m'envoie un mail si problème pendant 30 min, et tue le ou
les processus qui posent souci »).  Lancé et relancé par launchd (macos/surveillance/).

    python3 tools/surveillant.py [GULLIVER_HOME]           # la boucle, toutes les minutes
    python3 tools/surveillant.py [GULLIVER_HOME] --essai   # un relevé, ce qu'il en penserait, rien n'est arrêté

Toutes les minutes : la température de la puce (la sonde la plus chaude, par build/temperature, l'outil de
macos/temperature), la charge du processeur, et le temps de calcul de chaque programme (ps), regroupé par
exécutable.  Le Mac est « en souci » quand le processeur est occupé à SEUIL_CPU % ou plus, ou que la puce dépasse
SEUIL_TEMP °C.  Quand il l'a été pendant au moins 90 % des relevés des 30 dernières minutes :
  - les coupables sont les exécutables de l'utilisateur qui ont pris en moyenne au moins COEURS cœurs sur la demi-heure ;
  - ceux qui ne sont pas protégés reçoivent SIGTERM, puis SIGKILL 15 secondes plus tard s'ils vivent encore ;
  - un mail à l'administrateur de Gulliver (tools/mail_admin.py) donne les chiffres, ce qui a été arrêté, et ce qui
    était protégé ;
  - puis le surveillant repart d'une demi-heure vide.
Jamais arrêtés : les programmes du système (d'un autre utilisateur), le service de Gulliver, le tunnel de Cloudflare,
Proton Mail Bridge, VS Code, Claude, les programmes de Gulliver (build/macos-gulliver/bin), llama.cpp, MLX, et le
surveillant lui-même ; pendant la mesure du lundi (agent fr.fork.gulliver.bench-hebdo en marche), rien n'est arrêté
et aucun souci n'est compté.  Journal : build/surveillance/surveillant.log.
"""
import os
import signal
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mail_admin import send_admin  # noqa: E402

SEUIL_CPU = 80        # % de tout le processeur
SEUIL_TEMP = 95.0     # °C, la sonde la plus chaude de la puce
COEURS = 2.0          # cœurs pris en moyenne sur la demi-heure pour être un coupable
FENETRE = 30 * 60     # secondes
PART = 0.9            # part des relevés de la fenêtre en souci
PAS = 60              # secondes entre deux relevés

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../../../.."))
TEMP_TOOL = os.path.join(ROOT, "build", "temperature")
LOG_DIR = os.path.join(ROOT, "build", "surveillance")
BENCH_AGENT = "fr.fork.gulliver.bench-hebdo"
# Des morceaux du chemin des exécutables protégés.
PROTEGES = ("gulliver-service", "cloudflared", "Proton Mail Bridge", "bridge", "Visual Studio Code", "Code Helper",
            "/claude", "claude-code", "/build/macos-gulliver/bin/", "llama-", "mlx", "surveillant.py", "temperature",
            "Finder", "Dock", "loginwindow", "Terminal", "iTerm")


def say(text):
    print(time.strftime("%Y-%m-%d %H:%M:%S ") + text, flush=True)


def seconds(cputime):
    """« [jj-]hh:mm:ss.cc » ou « mm:ss.cc » en secondes."""
    days = 0
    if "-" in cputime:
        d, cputime = cputime.split("-", 1)
        days = int(d)
    total = 0.0
    for part in cputime.split(":"):
        total = total * 60 + float(part)
    return days * 86400 + total


def processes():
    """{pid: (uid, secondes de calcul, exécutable, protégé)} ; la protection se lit sur la ligne de commande entière
    (tous les scripts Python ont le même exécutable : le surveillant ne doit jamais se compter lui-même)."""
    out = subprocess.run(["ps", "-Ao", "pid=,uid=,time=,comm="], capture_output=True, text=True).stdout
    args = {}
    for line in subprocess.run(["ps", "-Ao", "pid=,args="], capture_output=True, text=True).stdout.splitlines():
        f = line.split(None, 1)
        if len(f) == 2 and f[0].isdigit():
            args[int(f[0])] = f[1]
    me = {os.getpid(), os.getppid()}
    procs = {}
    for line in out.splitlines():
        f = line.split(None, 3)
        if len(f) == 4:
            try:
                pid, uid = int(f[0]), int(f[1])
                safe = pid in me or uid != os.getuid() or any(p in f[3] or p in args.get(pid, "") for p in PROTEGES)
                procs[pid] = (uid, seconds(f[2]), f[3], safe)
            except ValueError:
                pass
    return procs


def machine():
    """(température de la puce en °C ou None, charge du processeur en % ou None)"""
    if not os.access(TEMP_TOOL, os.X_OK):
        subprocess.run(["clang", "-fobjc-arc", "-framework", "Foundation", "-framework", "IOKit", "-o", TEMP_TOOL,
                        os.path.join(ROOT, "macos", "temperature", "temperature.m")], capture_output=True)
    temp = cpu = None
    try:
        out = subprocess.run([TEMP_TOOL], capture_output=True, text=True, timeout=30).stdout
    except (OSError, subprocess.TimeoutExpired):
        return None, None
    for line in out.splitlines():
        if line.startswith("puce :"):
            try:
                temp = float(line.split()[2])
            except (IndexError, ValueError):
                pass
        elif line.startswith("charge :"):
            try:
                cpu = float(line.split()[3])
            except (IndexError, ValueError):
                pass
    return temp, cpu


def bench_running():
    out = subprocess.run(["launchctl", "print", f"gui/{os.getuid()}/{BENCH_AGENT}"], capture_output=True,
                         text=True).stdout
    return "state = running" in out


def stop(pids):
    """SIGTERM, puis SIGKILL aux survivants ; rend les pid arrêtés."""
    for pid in pids:
        try:
            os.kill(pid, signal.SIGTERM)
        except OSError:
            pass
    time.sleep(15)
    for pid in pids:
        try:
            os.kill(pid, 0)
            os.kill(pid, signal.SIGKILL)
        except OSError:
            pass
    time.sleep(1)
    # Encore là : vivant et pas un fantôme (Z, arrêté mais pas encore relevé par son parent).
    out = subprocess.run(["ps", "-o", "pid=,stat=", "-p", ",".join(str(p) for p in pids)], capture_output=True,
                         text=True).stdout if pids else ""
    alive = {int(f[0]) for f in (l.split() for l in out.splitlines()) if len(f) == 2 and not f[1].startswith("Z")}
    return [p for p in pids if p not in alive]


class Watch:
    def __init__(self, home, only=None):
        self.home = home
        self.only = only           # pour les tests : les seuls pid que le surveillant peut voir et arrêter
        self.samples = []          # (instant, en souci, température, charge, {pid: (uid, secondes, exécutable)})

    def tick(self, act=True):
        now = time.time()
        procs = processes()
        if bench_running():
            # La mesure du lundi chauffe le Mac exprès : rien n'est compté pendant qu'elle tourne.
            self.samples = []
            return
        temp, cpu = machine()
        trouble = (cpu is not None and cpu >= SEUIL_CPU) or (temp is not None and temp >= SEUIL_TEMP)
        self.samples.append((now, trouble, temp, cpu, procs))
        while self.samples and self.samples[0][0] < now - FENETRE:
            self.samples.pop(0)
        span = now - self.samples[0][0]
        bad = sum(1 for x in self.samples if x[1])
        if span >= FENETRE - 1.5 * PAS and bad >= PART * len(self.samples):
            self.act(now, span, procs, act)

    def culprits(self, span, procs, before):
        """Les exécutables qui ont pris au moins COEURS cœurs en moyenne entre before et procs."""
        groups = {}
        for pid, (uid, secs, path, safe) in procs.items():
            if self.only is not None and pid not in self.only:
                continue
            used = secs - (before[pid][1] if pid in before and before[pid][2] == path else 0.0)
            # Les protégés et les autres d'un même exécutable font deux groupes.
            g = groups.setdefault((path, safe), {"path": path, "safe": safe, "secs": 0.0, "pids": []})
            g["secs"] += max(0.0, used)
            g["pids"].append(pid)
        heavy = [(g["path"], g) for g in groups.values() if g["secs"] / span >= COEURS]
        return sorted(heavy, key=lambda x: -x[1]["secs"])

    def act(self, now, span, procs, act):
        temps = [s[2] for s in self.samples if s[2] is not None]
        cpus = [s[3] for s in self.samples if s[3] is not None]
        heavy = self.culprits(span, procs, self.samples[0][4])
        lines, stopped, kept = [], [], []
        for path, g in heavy:
            cores = g["secs"] / span
            name = os.path.basename(path) or path
            if g["safe"]:
                kept.append(f"{name} ({len(g['pids'])} processus, {cores:.1f} cœurs en moyenne) : protégé, laissé")
                continue
            done = stop(g["pids"]) if act else []
            left = f", {len(g['pids']) - len(done)} encore là" if act and len(done) < len(g["pids"]) else ""
            stopped.append(f"{name} ({len(g['pids'])} processus{left}, {cores:.1f} cœurs en moyenne, {path})")
        summary = (f"processeur {sum(cpus) / len(cpus):.0f} % en moyenne" if cpus else "processeur ?") + ", " + \
            (f"puce {sum(temps) / len(temps):.1f} °C en moyenne, {max(temps):.1f} °C au plus" if temps else "puce ?")
        say(f"souci depuis {span / 60:.0f} min : {summary}")
        for s in stopped:
            say(("arrêté : " if act else "arrêterait : ") + s)
        for k in kept:
            say(k)
        if not act:
            return
        lines.append(f"Depuis {span / 60:.0f} minutes, le Mac est en souci : {summary} "
                     f"(seuils : processeur {SEUIL_CPU} %, puce {SEUIL_TEMP:.0f} °C).")
        lines.append("")
        lines += (["Arrêtés :"] + [f"  - {s}" for s in stopped]) if stopped else ["Rien n'a été arrêté."]
        if kept:
            lines += ["", "Gros consommateurs protégés, laissés en marche :"] + [f"  - {k}" for k in kept]
        body = "Bonjour,\n\n" + "\n".join(lines) + "\n\nLe journal : build/surveillance/surveillant.log.\n\nGulliver\n"
        try:
            send_admin(self.home, "Gulliver : le Mac était en surchauffe" if not stopped else
                       f"Gulliver : {len(stopped)} programme{'s' if len(stopped) > 1 else ''} arrêté{'s' if len(stopped) > 1 else ''} sur le Mac", body)
            say("mail envoyé à l'administrateur")
        except Exception as e:
            say(f"mail pas envoyé : {type(e).__name__}")
        self.samples = []


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    home = args[0] if args else os.environ.get("GULLIVER_HOME", os.path.join(ROOT, "build", "gulliver-home"))
    w = Watch(home)
    if "--essai" in sys.argv:
        temp, cpu = machine()
        say(f"puce {temp} °C, processeur {cpu} % ; mesure du lundi en marche : {bench_running()}")
        before = processes()
        time.sleep(10)
        for path, g in w.culprits(10, processes(), before):
            say(f"{g['secs'] / 10:.1f} cœurs : {path} ({len(g['pids'])} processus)"
                + (" [protégé]" if g["safe"] else ""))
        return
    os.makedirs(LOG_DIR, exist_ok=True)
    say(f"surveillant lancé : processeur {SEUIL_CPU} %, puce {SEUIL_TEMP:.0f} °C, {FENETRE // 60} min, {COEURS} cœurs")
    while True:
        try:
            w.tick()
        except Exception as e:   # un relevé raté ne doit pas arrêter la surveillance
            say(f"relevé raté : {type(e).__name__}: {e}")
        time.sleep(PAS)


if __name__ == "__main__":
    main()
