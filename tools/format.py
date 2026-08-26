#!/usr/bin/env python3

import argparse
import re
import subprocess
import sys
from pathlib import Path

from format_common import load_ignore_patterns, repo_root, tracked_source_files

DEFAULT_CLANG_FORMAT = Path(
    r"F:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-format.exe"
)
ENV_BAT = "env.bat"
CLANG_FORMAT_VAR = "CLANG_FORMAT"
SCRIPT_DIR = Path(__file__).resolve().parent
POST_FORMAT_SCRIPT = SCRIPT_DIR / "post_format.py"


def load_env_bat(root: Path) -> dict[str, str]:
    path = root / ENV_BAT
    if not path.is_file():
        return {}
    values: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        m = re.match(r'\s*set\s+"?([A-Za-z_][A-Za-z0-9_]*)\s*=\s*"?([^"\r\n]*)', line)
        if m:
            values[m.group(1)] = m.group(2).rstrip()
    return values


def default_clang_format(root: Path) -> Path:
    env = load_env_bat(root)
    if CLANG_FORMAT_VAR in env and env[CLANG_FORMAT_VAR]:
        return Path(env[CLANG_FORMAT_VAR])
    return Path(sys.environ.get(CLANG_FORMAT_VAR, DEFAULT_CLANG_FORMAT))


def run_post_format(dry_run: bool) -> None:
    if not POST_FORMAT_SCRIPT.is_file():
        raise SystemExit(f"post-format script not found: {POST_FORMAT_SCRIPT}")

    cmd = [sys.executable, str(POST_FORMAT_SCRIPT)]
    if dry_run:
        cmd.append("--dry-run")
    subprocess.run(cmd, check=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("-n", "--dry-run", action="store_true")
    parser.add_argument("--clang-format", type=Path, default=None)
    parser.add_argument("--no-post-format", action="store_true")
    args = parser.parse_args()

    root = repo_root()
    cf = args.clang_format or default_clang_format(root)
    if not cf.is_file():
        raise SystemExit(f"clang-format not found: {cf}")

    patterns = load_ignore_patterns(root)
    files, skipped = tracked_source_files(root, patterns)

    processed = 0
    for abs_path in files:
        processed += 1
        if args.dry_run:
            subprocess.run([str(cf), "--dry-run", "-Werror", str(abs_path)])
        else:
            subprocess.run([str(cf), "-i", str(abs_path)], check=True)

    print(f"clang-format: {processed} file(s) processed, {skipped} skipped (ignored).")

    if args.no_post_format:
        print("post-format: skipped (--no-post-format).")
    else:
        run_post_format(args.dry_run)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
