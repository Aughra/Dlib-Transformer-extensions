# Fork for macOS of Dlib-Transformer-extensions (Albireo for Apple Silicon).
# License: Boost Software License   See LICENSE.txt for the full license.
#
# Sets the version of Albireo in the places the build compares (common/version.h, its numbers and
# its text; project() of CMakeLists.txt; web/openapi.json, written again by make_openapi.py), and
# checks that web/changelog.md has a section for it.
#
#   python3 tools/bump_version.py patch        # 2.0.0 -> 2.0.1: a fix, a change of the documentation of the page
#   python3 tools/bump_version.py minor        # 2.0.1 -> 2.1.0: a new function, a new setting
#   python3 tools/bump_version.py major        # 2.1.0 -> 3.0.0: a change that breaks what worked
#   python3 tools/bump_version.py 2.0.0        # an explicit version

import os
import re
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.join(here, "..")
version_h = os.path.join(root, "common", "version.h")
cmake = os.path.join(root, "CMakeLists.txt")
changelog = os.path.join(root, "web", "changelog.md")

with open(version_h, encoding="utf-8") as f:
    header = f.read()
current = [int(re.search(r"#define ALBIREO_VERSION_%s +(\d+)" % p, header).group(1)) for p in ("MAJOR", "MINOR", "PATCH")]

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
    header = re.sub(r"(#define ALBIREO_VERSION_%s +)\d+" % part, r"\g<1>%d" % value, header)
header = re.sub(r'(#define ALBIREO_VERSION +")[0-9.]+(")', r"\g<1>%s\g<2>" % text, header)
with open(version_h, "w", encoding="utf-8") as f:
    f.write(header)

with open(cmake, encoding="utf-8") as f:
    lists = f.read()
lists = re.sub(r"(project\(albireo VERSION )[0-9.]+", r"\g<1>%s" % text, lists, count=1)
with open(cmake, "w", encoding="utf-8") as f:
    f.write(lists)

subprocess.run([sys.executable, os.path.join(here, "make_openapi.py")], check=True)

print("Albireo %d.%d.%d -> %s" % (tuple(current) + (text,)))
with open(changelog, encoding="utf-8") as f:
    if not re.search(r"^## \[?%s\]?" % re.escape(text), f.read(), re.M):
        print("web/changelog.md has no section for %s yet: add it." % text)
