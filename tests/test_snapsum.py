"""tools/snapsum against a store written by gdc::store (ctest fixture "store", test_store.cpp)."""
import json
import os
import subprocess
import sys
import unittest

TOOL = os.environ["GDC_SNAPSUM"]
STORE = os.environ["GDC_STORE_OUT"]


def run(*args):
    r = subprocess.run([sys.executable, "-I", TOOL, *args], capture_output=True, text=True)
    return r.returncode, r.stdout, r.stderr


class SnapsumTest(unittest.TestCase):
    def test_summary(self):
        code, out, err = run(STORE, "--last", "0")
        self.assertEqual(code, 0, err)
        self.assertIn("== Nix: 3 records in 1 segment(s)", out)
        self.assertIn("#0000 full  12:00:00  [load]", out)
        self.assertIn("baseline: level 96", out)
        self.assertIn("#0001 delta 12:00:05  [equip]", out)
        self.assertIn("c031_ring -> b201e_ring", out)  # finger 2 swap
        self.assertIn("#0002 delta 12:00:09  [level]", out)
        self.assertIn("level 96->97", out)
        self.assertLess(len(out.splitlines()), 25)
        code, out, _ = run(STORE, "--last", "1")
        self.assertNotIn("#0001", out)
        self.assertIn("#0002", out)

    def test_full_rebuilds_the_snapshot(self):
        code, out, err = run(STORE, "--full", "20261114-120000-000-0001")
        self.assertEqual(code, 0, err)
        got = json.loads(out)
        with open(os.path.join(STORE, "expected-0001.json")) as f:
            want = json.load(f)
        self.assertEqual(sorted(json.dumps(x, sort_keys=True) for x in got["equipped"]),
                         sorted(json.dumps(x, sort_keys=True) for x in want["equipped"]))
        for k in ("char_attributes", "defense", "skills"):
            self.assertEqual(len(got[k]), len(want[k]), k)
        self.assertEqual(got["character"], want["character"])

    def test_errors(self):
        self.assertEqual(run(STORE, "--full", "nope")[0], 2)
        self.assertEqual(run(STORE, "--full", "20261114-120000-000-0009")[0], 1)
        self.assertEqual(run(os.path.join(STORE, "missing"))[0], 2)


if __name__ == "__main__":
    unittest.main()
