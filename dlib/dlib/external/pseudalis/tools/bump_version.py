# Fork pour macOS de Dlib-Transformer-extensions (Pseudalis pour Apple Silicon).
# License: Boost Software License   See LICENSE.txt for the full license.
#
# Fixe la version de Pseudalis aux endroits qui la portent (common/version.h, ses numéros et son texte ;
# project() de CMakeLists.txt ; "version" de web/openapi.json), et vérifie que web/changelog.md a une section
# pour elle.  Le fork numérote Pseudalis à part de l'original à partir de 2.0.0 (2026-11-15).
#
#   python3 tools/bump_version.py patch        # 2.0.0 -> 2.0.1 : une correction, une retouche de texte
#   python3 tools/bump_version.py minor        # 2.0.1 -> 2.1.0 : une nouvelle fonction, un nouveau réglage
#   python3 tools/bump_version.py major        # 2.1.0 -> 3.0.0 : un changement qui casse ce qui marchait
#   python3 tools/bump_version.py 2.0.0        # une version explicite

import os
import re
import sys

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.join(here, "..")
version_h = os.path.join(root, "common", "version.h")
cmake = os.path.join(root, "CMakeLists.txt")
openapi = os.path.join(root, "web", "openapi.json")
changelog = os.path.join(root, "web", "changelog.md")

with open(version_h, encoding="utf-8") as f:
    header = f.read()
current = [int(re.search(r"#define PSEUDALIS_VERSION_%s +(\d+)" % p, header).group(1)) for p in ("MAJOR", "MINOR", "PATCH")]

if len(sys.argv) != 2:
    sys.exit("usage: bump_version.py major|minor|patch|X.Y.Z (now %d.%d.%d)" % tuple(current))
arg = sys.argv[1]
if arg == "major":
    new = [current[0] + 1, 0, 0]
elif arg == "minor":
    new = [current[0], current[1] + 1, 0]
elif arg == "patch":
    new = [current[0], current[1], current[2] + 1]
elif re.fullmatch(r"\d+\.\d+\.\d+", arg):
    new = [int(x) for x in arg.split(".")]
else:
    sys.exit("usage: bump_version.py major|minor|patch|X.Y.Z")
text = "%d.%d.%d" % tuple(new)

for part, value in zip(("MAJOR", "MINOR", "PATCH"), new):
    header = re.sub(r"(#define PSEUDALIS_VERSION_%s +)\d+" % part, r"\g<1>%d" % value, header)
header = re.sub(r'(#define PSEUDALIS_VERSION +")[0-9.]+(")', r"\g<1>%s\g<2>" % text, header)
with open(version_h, "w", encoding="utf-8") as f:
    f.write(header)

with open(cmake, encoding="utf-8") as f:
    lists = f.read()
lists = re.sub(r"(project\(pseudalis VERSION )[0-9.]+", r"\g<1>%s" % text, lists, count=1)
with open(cmake, "w", encoding="utf-8") as f:
    f.write(lists)

with open(openapi, encoding="utf-8") as f:
    spec = f.read()
spec = re.sub(r'("version": ")[0-9.]+(")', r"\g<1>%s\g<2>" % text, spec, count=1)
with open(openapi, "w", encoding="utf-8") as f:
    f.write(spec)

print("Pseudalis %d.%d.%d -> %s" % (tuple(current) + (text,)))
if not os.path.exists(changelog) or not re.search(r"^## %s" % re.escape(text), open(changelog, encoding="utf-8").read(), re.M):
    print("web/changelog.md has no section for %s yet: add it." % text)
