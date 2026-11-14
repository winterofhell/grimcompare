import importlib.util
import os
import sys
import unittest
from pathlib import Path

_path = Path(os.environ.get("GDC_SCRIPTS", Path(__file__).resolve().parent.parent / "scripts")) / "launch_options.py"
_spec = importlib.util.spec_from_file_location("launch_options", _path)
lo = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(lo)

CURRENT = 'WINEDLLOVERRIDES="dxgi=n,b" LD_PRELOAD="" AMD_USERQ=1 PROTON_ENABLE_WAYLAND=1 DXVK_HDR=1 gamemoderun %command%'
MERGED = 'WINEDLLOVERRIDES="dxgi,dinput8=n,b" LD_PRELOAD="" AMD_USERQ=1 PROTON_ENABLE_WAYLAND=1 DXVK_HDR=1 gamemoderun %command%'


class Merge(unittest.TestCase):
    def test_user_install(self):
        self.assertEqual(lo.merge(CURRENT), MERGED)

    def test_idempotent(self):
        self.assertEqual(lo.merge(MERGED), MERGED)

    def test_empty(self):
        self.assertEqual(lo.merge(""), 'WINEDLLOVERRIDES="dinput8=n,b" %command%')

    def test_no_overrides(self):
        self.assertEqual(lo.merge("gamemoderun %command%"), 'WINEDLLOVERRIDES="dinput8=n,b" gamemoderun %command%')

    def test_unquoted_and_other_modes(self):
        self.assertEqual(lo.merge("WINEDLLOVERRIDES=d3d9=n;dxgi=n,b %command% -x"),
                         'WINEDLLOVERRIDES="d3d9=n;dxgi,dinput8=n,b" %command% -x')

    def test_dinput8_with_other_mode_is_moved(self):
        self.assertEqual(lo.merge('WINEDLLOVERRIDES="dinput8=b" %command%'),
                         'WINEDLLOVERRIDES="dinput8=n,b" %command%')


class Remove(unittest.TestCase):
    def test_roundtrip(self):
        self.assertEqual(lo.remove(MERGED), CURRENT)

    def test_drops_empty_variable(self):
        self.assertEqual(lo.remove('WINEDLLOVERRIDES="dinput8=n,b" %command%'), "%command%")

    def test_untouched_without_dinput8(self):
        self.assertEqual(lo.remove(CURRENT), CURRENT)


class Vdf(unittest.TestCase):
    def test_launch_options_path(self):
        text = r'''"UserLocalConfigStore" { "Software" { "Valve" { "Steam" { "apps" {
            "10" { "LaunchOptions" "nope" }
            "219990" { "LastPlayed" "1" "LaunchOptions" "WINEDLLOVERRIDES=\"dxgi=n,b\" %command%" } } } } } }'''
        d = lo.parse_vdf(text)
        apps = d["userlocalconfigstore"]["software"]["valve"]["steam"]["apps"]
        self.assertEqual(apps["219990"]["launchoptions"], 'WINEDLLOVERRIDES="dxgi=n,b" %command%')


if __name__ == "__main__":
    unittest.main()
