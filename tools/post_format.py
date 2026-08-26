#!/usr/bin/env python3

import argparse
import re
from pathlib import Path

from format_common import (
    SOURCE_EXTENSIONS,
    load_ignore_patterns,
    repo_root,
    tracked_source_files,
)

HOOK_PREFIXES = ("SAS_HOOK_CATCH_VOID", "SAS_HOOK_CATCH_RET")
CASE_LABEL_RE = re.compile(r"^(case\b.*:|default:)\s*$")


def normalize_paths(root: Path, requested: list[Path]) -> list[Path]:
    files: list[Path] = []
    for file_path in requested:
        abs_path = file_path if file_path.is_absolute() else (root / file_path)
        if abs_path.suffix.lower() not in SOURCE_EXTENSIONS:
            continue
        if abs_path.is_file():
            files.append(abs_path)
    return files


def split_eol(line: str) -> tuple[str, str]:
    if line.endswith("\r\n"):
        return line[:-2], "\r\n"
    if line.endswith("\n"):
        return line[:-1], "\n"
    return line, ""


def should_join(next_line_text: str) -> bool:
    stripped = next_line_text.lstrip()
    return any(stripped.startswith(prefix) for prefix in HOOK_PREFIXES)


def rewrite_hook_catch_lines(text: str) -> tuple[str, int]:
    lines = text.splitlines(keepends=True)
    if not lines:
        return text, 0

    out: list[str] = []
    i = 0
    changes = 0

    while i < len(lines):
        cur_text, cur_eol = split_eol(lines[i])
        if i + 1 < len(lines):
            next_text, next_eol = split_eol(lines[i + 1])
            if cur_text.strip() == "}" and should_join(next_text):
                indent_len = len(cur_text) - len(cur_text.lstrip(" \t"))
                indent = cur_text[:indent_len]
                merged_eol = next_eol or cur_eol
                out.append(f"{indent}}} {next_text.lstrip()}{merged_eol}")
                i += 2
                changes += 1
                continue
        out.append(lines[i])
        i += 1

    return "".join(out), changes


def leading_ws_len(text: str) -> int:
    return len(text) - len(text.lstrip(" \t"))


def is_case_label(text: str) -> bool:
    stripped = text.lstrip()
    return bool(CASE_LABEL_RE.match(stripped))


def is_break_statement(text: str) -> bool:
    stripped = text.strip()
    return stripped == "break;" or stripped.startswith("break; //")


def rewrite_case_blocks(text: str) -> tuple[str, int]:
    lines = text.splitlines(keepends=True)
    if not lines:
        return text, 0

    i = 0
    changes = 0

    while i < len(lines):
        cur_text, cur_eol = split_eol(lines[i])
        if not is_case_label(cur_text):
            i += 1
            continue

        if cur_text.rstrip().endswith("{"):
            i += 1
            continue

        case_indent_len = leading_ws_len(cur_text)

        first_stmt = i + 1
        while first_stmt < len(lines):
            first_text, _ = split_eol(lines[first_stmt])
            if first_text.strip():
                break
            first_stmt += 1

        if first_stmt >= len(lines):
            i += 1
            continue

        first_text, _ = split_eol(lines[first_stmt])
        first_stripped = first_text.lstrip()
        if first_stripped.startswith("{") or is_case_label(first_text):
            i += 1
            continue

        break_idx = -1
        scan = first_stmt
        while scan < len(lines):
            scan_text, _ = split_eol(lines[scan])
            scan_stripped = scan_text.lstrip()

            if is_case_label(scan_text) and leading_ws_len(scan_text) <= case_indent_len:
                break
            if scan_stripped.startswith("}") and leading_ws_len(scan_text) <= case_indent_len:
                break
            if is_break_statement(scan_text):
                break_idx = scan
                break
            scan += 1

        if break_idx == -1:
            i += 1
            continue

        close_indent = cur_text[:case_indent_len]
        label = cur_text.rstrip()
        lines[i] = f"{label} {{{cur_eol}"

        _, break_eol = split_eol(lines[break_idx])
        lines.insert(break_idx + 1, f"{close_indent}}}{break_eol or cur_eol}")
        changes += 1
        i = break_idx + 2

    return "".join(lines), changes


def rewrite_content(text: str) -> tuple[str, int]:
    updated, case_changes = rewrite_case_blocks(text)
    updated, hook_changes = rewrite_hook_catch_lines(updated)
    return updated, case_changes + hook_changes


def process_file(path: Path, dry_run: bool) -> int:
    original = path.read_text(encoding="utf-8")
    updated, changes = rewrite_content(original)
    if changes and not dry_run:
        path.write_text(updated, encoding="utf-8")
    return changes


def process_files(files: list[Path], root: Path, dry_run: bool) -> tuple[int, int]:
    touched_files = 0
    total_changes = 0
    for path in files:
        changes = process_file(path, dry_run)
        if changes:
            touched_files += 1
            total_changes += changes
            rel = path.relative_to(root)
            mode = "would update" if dry_run else "updated"
            print(f"{mode}: {rel} ({changes} rewrite(s))")
    return touched_files, total_changes


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="*", type=Path)
    parser.add_argument("-n", "--dry-run", action="store_true")
    args = parser.parse_args()

    root = repo_root()
    patterns = load_ignore_patterns(root)

    if args.paths:
        files = normalize_paths(root, [Path(p) for p in args.paths])
    else:
        files, _ = tracked_source_files(root, patterns)

    touched_files, total_changes = process_files(files, root, args.dry_run)

    mode = "would make" if args.dry_run else "made"
    print(f"post-format: {mode} {total_changes} rewrite(s) across {touched_files} file(s).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
