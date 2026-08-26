#!/usr/bin/env python3

import fnmatch
import subprocess
from pathlib import Path

SOURCE_EXTENSIONS = (".cpp", ".h", ".hpp", ".cc", ".cxx", ".cu", ".cuh")


def repo_root() -> Path:
    try:
        return Path(subprocess.check_output(["git", "rev-parse", "--show-toplevel"], text=True).strip())
    except subprocess.CalledProcessError:
        raise SystemExit("format scripts must be run inside the git repository")


def load_ignore_patterns(root: Path) -> list[str]:
    path = root / ".clang-format-ignore"
    if not path.is_file():
        return []
    return [
        line.strip()
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.startswith("#")
    ]


def _segment_matches(pattern: str, name: str) -> bool:
    return fnmatch.fnmatch(name, pattern)


def _segments_match(parts: tuple[str, ...], start: int, segs: list[str]) -> bool:
    """True when `segs` line up exactly with `parts[start:start + len(segs)]`."""
    return all(_segment_matches(segs[i], parts[start + i]) for i in range(len(segs)))


def _segments_prefix_match(parts: tuple[str, ...], start: int, segs: list[str]) -> bool:
    """True when the trailing `parts` are a prefix of `segs` (used for dir-only patterns)."""
    return all(_segment_matches(segs[i], parts[start + i]) for i in range(len(parts) - start))


def _pattern_matches(rel_parts: tuple[str, ...], pattern: str) -> bool:
    raw = pattern.lstrip("/")
    if not raw or raw == "/":
        return False

    anchored = pattern.startswith("/")
    dir_only = pattern.endswith("/")
    segs = [s for s in raw.split("/") if s]
    if not segs:
        return False

    n = len(segs)
    starts = (0,) if anchored else range(len(rel_parts) + 1)

    for start in starts:
        end = start + n
        if end > len(rel_parts):
            if dir_only and start < len(rel_parts) and _segments_prefix_match(rel_parts, start, segs):
                return True
            continue
        if _segments_match(rel_parts, start, segs) and (
            dir_only or end == len(rel_parts) or "/" not in raw
        ):
            return True

    return False


def is_ignored(relative: Path, patterns: list[str]) -> bool:
    parts = relative.parts
    ignored = False
    for pattern in patterns:
        if pattern.startswith("!"):
            if _pattern_matches(parts, pattern[1:]):
                ignored = False
        elif _pattern_matches(parts, pattern):
            ignored = True
    return ignored


def tracked_source_files(root: Path, ignore_patterns: list[str]) -> tuple[list[Path], int]:
    try:
        listed = subprocess.check_output(
            ["git", "-C", str(root), "ls-files", "--"] + [f"*{e}" for e in SOURCE_EXTENSIONS],
            text=True,
        ).splitlines()
    except subprocess.CalledProcessError as exc:
        raise SystemExit(f"git ls-files failed: {exc}")

    files: list[Path] = []
    skipped = 0
    for rel in listed:
        path = Path(rel)
        if is_ignored(path, ignore_patterns):
            skipped += 1
            continue
        files.append(root / path)
    return files, skipped
