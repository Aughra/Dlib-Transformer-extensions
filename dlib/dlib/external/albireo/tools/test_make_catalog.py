import contextlib
import hashlib
import importlib.util
import io
import json
import os
import sys
import tempfile
import unittest
from unittest import mock


HERE = os.path.dirname(os.path.abspath(__file__))
MAKE_CATALOG = os.path.join(HERE, "make_catalog.py")
HUB_API = "https://huggingface.co/api/models/"


def load_make_catalog():
    spec = importlib.util.spec_from_file_location("albireo_make_catalog", MAKE_CATALOG)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class MakeCatalogTest(unittest.TestCase):
    def setUp(self):
        self.module = load_make_catalog()

    def test_builds_catalog_with_mocked_hub_responses(self):
        files_by_repo = {}
        for model in self.module.MODELS:
            if "hub" not in model:
                continue
            repo, filename = model["hub"]
            files_by_repo.setdefault(repo, set()).add(filename)
            if "projector" in model:
                projector_repo, projector_file, _ = model["projector"]
                files_by_repo.setdefault(projector_repo, set()).add(projector_file)

        responses = {}
        commits = {}
        hashes = {}
        for repo, files in files_by_repo.items():
            commit = "mock-" + repo.replace("/", "-")
            commits[repo] = commit
            responses[HUB_API + repo] = {
                "sha": commit,
                "gguf": {"architecture": "llama", "total": 7, "context_length": 4096},
            }
            tree = []
            for filename in sorted(files):
                digest = hashlib.sha256((repo + "/" + filename).encode("utf-8")).hexdigest()
                hashes[(repo, filename)] = digest
                tree.append({"path": filename, "lfs": {"size": len(filename) * 1024, "oid": digest}})
            responses[HUB_API + repo + "/tree/" + commit] = tree

        with tempfile.TemporaryDirectory() as folder:
            output_path = os.path.join(folder, "catalog.json")
            get_json = mock.Mock(side_effect=lambda url: responses[url])
            stdout = io.StringIO()
            with mock.patch.object(self.module, "get_json", get_json), \
                    mock.patch.object(sys, "argv", ["make_catalog.py", output_path]), \
                    mock.patch.object(self.module.urllib.request, "urlopen",
                                      side_effect=AssertionError("network access is disabled")), \
                    contextlib.redirect_stdout(stdout):
                result = self.module.main()

            self.assertEqual(result, 0)
            with open(output_path, encoding="utf-8") as stream:
                catalog = json.load(stream)

        self.assertEqual(catalog["format"], 1)
        self.assertEqual(len(catalog["models"]), len(self.module.MODELS))
        entries = {entry["id"]: entry for entry in catalog["models"]}
        for model in self.module.MODELS:
            if "hub" not in model:
                continue
            repo, filename = model["hub"]
            entry = entries[model["id"]]
            self.assertEqual(entry["arch"], "llama")
            self.assertEqual(entry["parameters"], 7)
            self.assertEqual(entry["context"], 4096)
            self.assertEqual(entry["sha256"], hashes[(repo, filename)])
            self.assertEqual(entry["files"][0]["url"],
                             "https://huggingface.co/%s/resolve/%s/%s" % (repo, commits[repo], filename))
            if "projector" in model:
                projector_repo, projector_file, _ = model["projector"]
                self.assertEqual(entry["projector"]["sha256"], hashes[(projector_repo, projector_file)])
                self.assertEqual(entry["projector"]["files"][0]["url"],
                                 "https://huggingface.co/%s/resolve/%s/%s"
                                 % (projector_repo, commits[projector_repo], projector_file))

        self.assertEqual(get_json.call_count, 2 * len(files_by_repo))
        self.assertTrue(stdout.getvalue())


if __name__ == "__main__":
    unittest.main()
