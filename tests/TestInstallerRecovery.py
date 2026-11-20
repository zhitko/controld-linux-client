import hashlib
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "installer", Path(__file__).resolve().parents[1] / "packaging/install_system_files.py"
)
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class InstallerRecovery(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "root"
        self.root.mkdir()
        self.stage = Path(self.temp.name) / "stage"
        self.stage.mkdir()
        env = patch.dict(os.environ, {
            "CONTROLD_TRAY_INSTALL_ROOT": str(self.root),
            "CONTROLD_TRAY_TEST_INSTALL": "allow-test-root",
        })
        env.start()
        self.addCleanup(env.stop)
        self.files = [
            ("0755", installer.HELPER_DEST, b"helper"),
            ("0644", installer.POLICY_DEST,
             (installer.ACTION_ID + installer.HELPER_DEST).encode()),
            ("0644", installer.LIB_DIR + "/library.so", b"library"),
        ]
        self.stage_files()

    def stage_files(self):
        manifest = []
        for mode, dest, data in self.files:
            name = Path(dest).name
            (self.stage / name).write_bytes(data)
            manifest.append(f"{mode} {dest} {name} {hashlib.sha256(data).hexdigest()}\n")
        (self.stage / "manifest").write_text("".join(manifest))

    def installed(self, dest):
        return self.root / dest.lstrip("/")

    def test_corrupt_last_source_writes_nothing(self):
        (self.stage / "library.so").write_bytes(b"corrupt")
        with self.assertRaises(SystemExit):
            installer.cmd_install(str(self.stage))
        for dest in [entry[1] for entry in self.files] + [installer.MANIFEST_DEST]:
            self.assertFalse(self.installed(dest).exists())

    def test_write_failure_rolls_back(self):
        real_write = installer.write_bytes_atomic
        def fail_library(data, destination, mode):
            if destination.endswith("/library.so"):
                raise OSError("simulated full disk")
            real_write(data, destination, mode)
        with patch.object(installer, "write_bytes_atomic", side_effect=fail_library):
            with self.assertRaises(OSError):
                installer.cmd_install(str(self.stage))
        for dest in [entry[1] for entry in self.files] + [installer.MANIFEST_DEST]:
            self.assertFalse(self.installed(dest).exists())

    def test_interrupted_install_has_manifest_and_can_be_repaired(self):
        real_write = installer.write_bytes_atomic
        def interrupted(data, destination, mode):
            if destination.endswith("/library.so"):
                raise KeyboardInterrupt()
            real_write(data, destination, mode)
        with patch.object(installer, "write_bytes_atomic", side_effect=interrupted):
            with self.assertRaises(KeyboardInterrupt):
                installer.cmd_install(str(self.stage))
        self.assertTrue(self.installed(installer.MANIFEST_DEST).exists())
        self.assertFalse(self.installed(self.files[2][1]).exists())
        installer.cmd_install(str(self.stage))
        for _mode, dest, data in self.files:
            self.assertEqual(self.installed(dest).read_bytes(), data)
        installer.cmd_uninstall()
        self.assertFalse(self.installed(installer.MANIFEST_DEST).exists())

    def test_failed_upgrade_restores_previous_files_and_manifest(self):
        installer.cmd_install(str(self.stage))
        previous = {dest: self.installed(dest).read_bytes()
                    for dest in [entry[1] for entry in self.files] + [installer.MANIFEST_DEST]}
        self.files = [(mode, dest, data + b" updated") for mode, dest, data in self.files]
        self.stage_files()
        real_write = installer.write_bytes_atomic
        def fail_new_library(data, destination, mode):
            if destination.endswith("/library.so") and data.endswith(b" updated"):
                raise OSError("simulated full disk")
            real_write(data, destination, mode)
        with patch.object(installer, "write_bytes_atomic", side_effect=fail_new_library):
            with self.assertRaises(OSError):
                installer.cmd_install(str(self.stage))
        for dest, data in previous.items():
            self.assertEqual(self.installed(dest).read_bytes(), data)

    def test_package_policy_is_preserved_even_when_identical(self):
        installer.cmd_install(str(self.stage))
        self.installed(installer.HELPER_DEST).write_bytes(b"packaged helper")
        listing = self.root / "var/lib/dpkg/info/controld-tray.list"
        listing.parent.mkdir(parents=True)
        listing.write_text(installer.HELPER_DEST + "\n" + installer.POLICY_DEST + "\n")
        with self.assertRaises(SystemExit):
            installer.cmd_install(str(self.stage))
        installer.cmd_uninstall()
        self.assertEqual(self.installed(installer.HELPER_DEST).read_bytes(), b"packaged helper")
        self.assertEqual(self.installed(installer.POLICY_DEST).read_bytes(), self.files[1][2])
        self.assertFalse(self.installed(self.files[2][1]).exists())

    def test_unrecognized_replacement_preserves_dependencies(self):
        installer.cmd_install(str(self.stage))
        self.installed(installer.HELPER_DEST).write_bytes(b"replacement")
        installer.cmd_uninstall()
        for _mode, dest, _data in self.files:
            self.assertTrue(self.installed(dest).exists())

    def test_package_owned_identical_helper_is_preserved(self):
        installer.cmd_install(str(self.stage))
        listing = self.root / "var/lib/dpkg/info/controld-tray.list"
        listing.parent.mkdir(parents=True)
        listing.write_text(installer.HELPER_DEST + "\n" + installer.POLICY_DEST + "\n")
        installer.cmd_uninstall()
        for _mode, dest, data in self.files[:2]:
            self.assertEqual(self.installed(dest).read_bytes(), data)


if __name__ == "__main__":
    unittest.main()
