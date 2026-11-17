import gzip
import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest


HERE = os.path.dirname(os.path.abspath(__file__))
PUBLISH_MODEL = os.path.join(HERE, "publish_model.py")


def gguf_string(value):
    encoded = value.encode("utf-8")
    return struct.pack("<Q", len(encoded)) + encoded


def synthetic_gguf():
    payload = bytes(range(256)) * 4096 + b"tail-data"
    model = bytearray(b"GGUF")
    model.extend(struct.pack("<IQQ", 3, 1, 2))
    model.extend(gguf_string("general.architecture"))
    model.extend(struct.pack("<I", 8))
    model.extend(gguf_string("llama"))
    model.extend(gguf_string("llama.context_length"))
    model.extend(struct.pack("<II", 4, 4096))
    model.extend(gguf_string("tensor"))
    model.extend(struct.pack("<IQ", 1, 3))
    model.extend(struct.pack("<IQ", 0, 0))
    model.extend(payload)
    return bytes(model)


class PublishModelTest(unittest.TestCase):
    def test_splits_and_hashes_local_gzip_parts(self):
        with tempfile.TemporaryDirectory() as folder:
            model_path = os.path.join(folder, "synthetic.gguf")
            output_path = os.path.join(folder, "parts")
            model = synthetic_gguf()
            with open(model_path, "wb") as stream:
                stream.write(model)

            result = subprocess.run(
                [
                    sys.executable,
                    "-B",
                    PUBLISH_MODEL,
                    model_path,
                    "synthetic",
                    output_path,
                    "--base",
                    "https://example.invalid/models",
                    "--part-mb",
                    "1",
                    "--gzip",
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            entry = json.loads(result.stdout)
            self.assertEqual(entry["arch"], "llama")
            self.assertEqual(entry["parameters"], 3)
            self.assertEqual(entry["context"], 4096)
            self.assertEqual(entry["source"], "https://example.invalid/models")
            self.assertEqual(entry["size"], len(model))
            self.assertEqual(entry["sha256"], hashlib.sha256(model).hexdigest())
            self.assertGreater(len(entry["files"]), 1)

            rebuilt = bytearray()
            for part in entry["files"]:
                self.assertEqual(part["compression"], "gzip")
                self.assertTrue(part["url"].startswith(entry["source"] + "/"))
                name = part["url"].rsplit("/", 1)[1]
                with open(os.path.join(output_path, name), "rb") as stream:
                    compressed = stream.read()
                self.assertEqual(len(compressed), part["compressed_size"])
                data = gzip.decompress(compressed)
                self.assertEqual(len(data), part["size"])
                self.assertEqual(hashlib.sha256(data).hexdigest(), part["sha256"])
                rebuilt.extend(data)

            self.assertEqual(bytes(rebuilt), bytes(model))

    def test_splits_local_parts_without_compression(self):
        with tempfile.TemporaryDirectory() as folder:
            model_path = os.path.join(folder, "synthetic.gguf")
            output_path = os.path.join(folder, "parts")
            model = synthetic_gguf()
            with open(model_path, "wb") as stream:
                stream.write(model)

            result = subprocess.run(
                [
                    sys.executable,
                    "-B",
                    PUBLISH_MODEL,
                    model_path,
                    "synthetic",
                    output_path,
                    "--base",
                    "https://example.invalid/models",
                    "--part-mb",
                    "1",
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            entry = json.loads(result.stdout)
            self.assertGreater(len(entry["files"]), 1)

            rebuilt = bytearray()
            for part in entry["files"]:
                self.assertNotIn("compression", part)
                self.assertNotIn("compressed_size", part)
                self.assertTrue(part["url"].startswith(entry["source"] + "/"))
                name = part["url"].rsplit("/", 1)[1]
                with open(os.path.join(output_path, name), "rb") as stream:
                    data = stream.read()
                self.assertEqual(len(data), part["size"])
                self.assertEqual(hashlib.sha256(data).hexdigest(), part["sha256"])
                rebuilt.extend(data)

            self.assertEqual(bytes(rebuilt), model)
            self.assertEqual(entry["size"], len(model))
            self.assertEqual(entry["sha256"], hashlib.sha256(model).hexdigest())

    def test_rejects_invalid_or_truncated_gguf_without_traceback(self):
        for name, contents in (("bad-magic.gguf", b"not a GGUF"), ("truncated.gguf", b"GGUF\x03")):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as folder:
                model_path = os.path.join(folder, name)
                output_path = os.path.join(folder, "parts")
                with open(model_path, "wb") as stream:
                    stream.write(contents)

                result = subprocess.run(
                    [
                        sys.executable,
                        "-B",
                        PUBLISH_MODEL,
                        model_path,
                        "synthetic",
                        output_path,
                        "--base",
                        "https://example.invalid/models",
                    ],
                    capture_output=True,
                    text=True,
                )
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stdout, "")
                self.assertTrue(result.stderr.startswith("error: "))
                self.assertNotIn("Traceback", result.stderr)
                self.assertFalse(os.path.exists(output_path))


if __name__ == "__main__":
    unittest.main()
