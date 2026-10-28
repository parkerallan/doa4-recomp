"""Replace git symlink stubs with real copies in a checkout and its submodules.

On Windows without symlink support (core.symlinks false, the default unless
Developer Mode is on), git checks a symlink out as a small text file holding
the link's target path. A few of rexglue-sdk's submodules (libmspack, o1heap)
use symlinks, and the build fails on those stubs. This copies each stub's
target over it. Safe to run again: files that are already real copies are left
alone.

usage: py -3 fix_git_symlinks.py <checkout>     (e.g. ..\\rexglue-sdk)
"""
import os
import shutil
import subprocess
import sys


def repos(root):
    out = subprocess.run(["git", "-C", root, "submodule", "status", "--recursive"],
                         capture_output=True, text=True, check=True).stdout
    return [root] + [os.path.join(root, line.split()[1]) for line in out.splitlines() if line.strip()]


def stub_target(path):
    """The target a symlink stub points at, or None if the file is not a stub."""
    if not os.path.isfile(path) or os.path.getsize(path) > 4096:
        return None
    with open(path, "rb") as f:
        text = f.read().decode("utf-8", "replace").strip()
    if not text or "\n" in text:
        return None
    target = os.path.normpath(os.path.join(os.path.dirname(path), text))
    return target if os.path.exists(target) else None


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    root = os.path.abspath(sys.argv[1])
    fixed = skipped = 0
    for repo in repos(root):
        files = subprocess.run(["git", "-C", repo, "ls-files", "-s"],
                               capture_output=True, text=True, check=True).stdout
        for line in files.splitlines():
            meta, rel = line.split("\t", 1)
            if not meta.startswith("120000"):
                continue
            path = os.path.join(repo, rel)
            target = stub_target(path)
            if target is None:
                skipped += 1  # a real symlink, or already replaced by a copy
                continue
            if os.path.isdir(target):
                os.remove(path)
                shutil.copytree(target, path)
            else:
                shutil.copyfile(target, path)
            fixed += 1
    print(f"replaced {fixed} symlink stub(s); {skipped} link(s) already fine")


main()
