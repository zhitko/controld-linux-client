#!/usr/bin/env python3
"""Install or remove the ControlD Tray Chrome helper files.

pkexec runs this with the root-owned system Python. pkexec does not forward
CONTROLD_TRAY_INSTALL_ROOT. A root test sets that variable and
CONTROLD_TRAY_TEST_INSTALL=allow-test-root. An unprivileged test sets only
CONTROLD_TRAY_INSTALL_ROOT.
"""

import hashlib
import os
import stat
import sys

ACTION_ID = "com.vzhitko.controldtray.chrome-policy"
HELPER_DEST = "/usr/libexec/controld-tray-chrome-policy"
POLICY_DEST = "/usr/share/polkit-1/actions/com.vzhitko.controldtray.policy"
LIB_DIR = "/usr/lib/controld-tray"
MANIFEST_DEST = LIB_DIR + "/manifest"
MAX_POLICY_BYTES = 256 * 1024


def fail(message, code=1):
    sys.stderr.write(message.rstrip() + "\n")
    raise SystemExit(code)


def components_safe(path):
    if not path.startswith("/"):
        return False
    parts = [part for part in path.split("/") if part != ""]
    return len(parts) > 0 and all(part not in (".", "..") for part in parts)


def name_safe(name):
    if name in ("", ".", "..") or "/" in name:
        return False
    return all(
        character.isascii() and (character.isalnum() or character in "._+-") for character in name
    )


def destination_allowed(path):
    if path in (HELPER_DEST, POLICY_DEST, MANIFEST_DEST):
        return True
    prefix = LIB_DIR + "/"
    if not path.startswith(prefix):
        return False
    return name_safe(path[len(prefix) :])


def install_root():
    root = os.environ.get("CONTROLD_TRAY_INSTALL_ROOT", "")
    if os.geteuid() == 0:
        if root == "" and os.environ.get("CONTROLD_TRAY_TEST_INSTALL", "") == "":
            return ""
        if os.environ.get("CONTROLD_TRAY_TEST_INSTALL", "") != "allow-test-root":
            fail("Refusing CONTROLD_TRAY_INSTALL_ROOT for a root install.")
    elif root == "":
        fail("This installer must run as root.")
    if not components_safe(root):
        fail("Invalid CONTROLD_TRAY_INSTALL_ROOT.")
    return root.rstrip("/")


def lstat_or_none(path):
    try:
        return os.lstat(path)
    except FileNotFoundError:
        return None


def refuse_symlink_parents(path):
    if not components_safe(path):
        fail("Unsafe path: %s" % path, 2)
    current = ""
    for part in [part for part in path.split("/") if part != ""]:
        current += "/" + part
        info = lstat_or_none(current)
        if info is None:
            return
        if stat.S_ISLNK(info.st_mode):
            fail("Refusing to follow a symlink: %s" % current, 2)


def ensure_dir(path):
    if not components_safe(path):
        fail("Refusing to create %s." % path, 2)
    current = ""
    for part in [part for part in path.split("/") if part != ""]:
        current += "/" + part
        info = lstat_or_none(current)
        if info is None:
            os.mkdir(current, 0o755)
            continue
        if stat.S_ISLNK(info.st_mode):
            fail("Refusing to use a symlink: %s" % current, 2)
        if not stat.S_ISDIR(info.st_mode):
            fail("Not a directory: %s" % current, 2)


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        while True:
            chunk = handle.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def regular_file(path):
    info = lstat_or_none(path)
    return info is not None and stat.S_ISREG(info.st_mode) and not stat.S_ISLNK(info.st_mode)


def policy_text_ok(path):
    if not regular_file(path):
        return False
    info = os.lstat(path)
    if info.st_size > MAX_POLICY_BYTES:
        return False
    with open(path, "rb") as handle:
        text = handle.read().decode("utf-8", "replace")
    return ACTION_ID in text and HELPER_DEST in text


def read_manifest(path):
    if not regular_file(path):
        fail("The install manifest is not a regular file.")
    entries = []
    with open(path, "r", encoding="utf-8") as handle:
        for line_number, raw in enumerate(handle, 1):
            line = raw.strip()
            if line == "":
                continue
            parts = line.split(" ")
            if len(parts) != 4:
                fail("Invalid manifest line %d." % line_number)
            mode_text, dest, name, digest = parts
            if mode_text not in ("0644", "0755"):
                fail("Invalid mode in the manifest.")
            if not destination_allowed(dest) or not components_safe(dest):
                fail("Manifest destination is not allowed: %s" % dest)
            if not name_safe(name) or dest.rsplit("/", 1)[-1] != name:
                fail("Manifest source name does not match the destination.")
            if len(digest) != 64 or any(character not in "0123456789abcdef" for character in digest):
                fail("Manifest hash is invalid.")
            entries.append((int(mode_text, 8), dest, name, digest))
    destinations = [item[1] for item in entries]
    if HELPER_DEST not in destinations or POLICY_DEST not in destinations:
        fail("The install manifest is missing the helper or the policy.")
    if MANIFEST_DEST in destinations:
        fail("The install manifest cannot describe itself.")
    if len(destinations) != len(set(destinations)):
        fail("The install manifest repeats a destination.")
    return entries


def read_file_bytes(path):
    if not regular_file(path):
        fail("Source is not a regular file: %s" % path, 2)
    with open(path, "rb") as handle:
        return handle.read()


def write_bytes_atomic(data, destination, mode):
    refuse_symlink_parents(destination)
    parent = destination.rsplit("/", 1)[0]
    ensure_dir(parent)
    existing = lstat_or_none(destination)
    if existing is not None and (stat.S_ISLNK(existing.st_mode) or not stat.S_ISREG(existing.st_mode)):
        fail("Refusing to replace a non-regular file: %s" % destination, 2)
    tmp = destination + ".tmp"
    tmp_info = lstat_or_none(tmp)
    if tmp_info is not None:
        if stat.S_ISLNK(tmp_info.st_mode) or not stat.S_ISREG(tmp_info.st_mode):
            fail("Refusing to use a non-regular temporary file for %s" % destination, 2)
        os.unlink(tmp)
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC, mode)
    ok = False
    try:
        offset = 0
        while offset < len(data):
            wrote = os.write(fd, data[offset:])
            if wrote <= 0:
                fail("Could not write %s." % destination, 2)
            offset += wrote
        os.fchmod(fd, mode)
        if os.geteuid() == 0:
            os.fchown(fd, 0, 0)
        os.fsync(fd)
        ok = True
    finally:
        os.close(fd)
        if not ok:
            try:
                os.unlink(tmp)
            except OSError:
                pass
    os.rename(tmp, destination)


def write_atomic(source, destination, mode, expected_hash):
    data = read_file_bytes(source)
    actual = hashlib.sha256(data).hexdigest()
    if actual != expected_hash:
        fail("Staged file does not match the manifest: %s" % source, 2)
    write_bytes_atomic(data, destination, mode)


def cmd_install(stage):
    if not components_safe(stage):
        fail("The staging directory is not an absolute path.")
    stage_info = lstat_or_none(stage)
    if stage_info is None or stat.S_ISLNK(stage_info.st_mode) or not stat.S_ISDIR(stage_info.st_mode):
        fail("The staging directory is not a directory.")
    stage = stage.rstrip("/")
    manifest_bytes = read_file_bytes(stage + "/manifest")
    entries = read_manifest(stage + "/manifest")
    policy_source = None
    for _mode, dest, name, _digest in entries:
        if dest == POLICY_DEST:
            policy_source = stage + "/" + name
    if policy_source is None or not policy_text_ok(policy_source):
        fail("The staged policy file is not the ControlD Tray polkit action.")
    root = install_root()
    for mode, dest, name, digest in entries:
        write_atomic(stage + "/" + name, root + dest, mode, digest)
    write_bytes_atomic(manifest_bytes, root + MANIFEST_DEST, 0o644)
    if read_file_bytes(root + MANIFEST_DEST) != manifest_bytes:
        fail("The installed manifest does not match the staged manifest.", 2)
    if not policy_text_ok(root + POLICY_DEST):
        fail("The installed policy file failed verification.", 2)


def remove_if_matches(path, expected_hash):
    refuse_symlink_parents(path)
    info = lstat_or_none(path)
    if info is None:
        return False
    if stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode):
        fail("Refusing to remove a non-regular file: %s" % path, 2)
    if sha256_file(path) != expected_hash:
        sys.stderr.write("Left %s in place because its contents changed.\n" % path)
        return False
    os.unlink(path)
    return True


def cmd_uninstall():
    root = install_root()
    manifest_path = root + MANIFEST_DEST
    refuse_symlink_parents(manifest_path)
    if not regular_file(manifest_path):
        fail("There is no ControlD Tray helper manifest to uninstall.")
    manifest_bytes = read_file_bytes(manifest_path)
    entries = read_manifest(manifest_path)
    for _mode, dest, _name, digest in entries:
        full = root + dest
        if dest == POLICY_DEST and regular_file(full) and not policy_text_ok(full):
            fail("Refusing to remove a policy file that is not the ControlD Tray action.", 2)
        remove_if_matches(full, digest)
    if regular_file(manifest_path) and read_file_bytes(manifest_path) == manifest_bytes:
        os.unlink(manifest_path)
    lib_dir = root + LIB_DIR
    info = lstat_or_none(lib_dir)
    if info is not None and stat.S_ISDIR(info.st_mode) and not stat.S_ISLNK(info.st_mode):
        try:
            os.rmdir(lib_dir)
        except OSError:
            pass


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "install":
        cmd_install(sys.argv[2])
        return
    if len(sys.argv) == 2 and sys.argv[1] == "uninstall":
        cmd_uninstall()
        return
    fail("Usage: install_system_files.py install <stage-dir> | uninstall")


if __name__ == "__main__":
    main()
