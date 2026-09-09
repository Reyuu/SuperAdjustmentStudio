#!/usr/bin/env python3

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
TRANSLATIONS = ROOT / "src" / "resources" / "translations.json"

KEYS = re.compile(r'\bt\s*\(\s*"([^"]+)"\s*\)|translate\s*\(\s*"([^"]+)"\s*\)')


def find_keys(src):
    found = set()
    for pattern in ("**/*.cpp", "**/*.h"):
        for path in src.glob(pattern):
            text = path.read_text(encoding="utf-8", errors="ignore")
            for key, alt in KEYS.findall(text):
                found.add(key or alt)
    return found


def flat_keys(tree, prefix=""):
    keys = set()
    for name, value in tree.items():
        full = prefix + "." + name if prefix else name
        if isinstance(value, dict):
            keys |= flat_keys(value, full)
        else:
            keys.add(full)
    return keys


def guess_english(key):
    last = key.split(".")[-1].replace("_", " ").strip()
    return last[0].upper() + last[1:] if last else key


def lookup(tree, parts, make=False):
    node = tree
    for part in parts[:-1]:
        child = node.get(part)
        if child is None and make:
            child = {}
            node[part] = child
        if not isinstance(child, dict):
            return None
        node = child
    return node


def put(tree, key, value, overwrite=False):
    parts = key.split(".")
    node = lookup(tree, parts, make=True)
    if node is None or isinstance(node.get(parts[-1]), dict):
        print(f'Key conflict: "{key}" is used as both text and a group. Rename one in code.')
        sys.exit(1)
    if overwrite or parts[-1] not in node:
        node[parts[-1]] = value
        return True
    return False


def save(path, translations):
    langs = sorted(l for l in translations if l != "en")
    if "en" in translations:
        langs.insert(0, "en")
    with open(path, "w", encoding="utf-8") as f:
        f.write("{\n")
        for i, lang in enumerate(langs):
            block = json.dumps(translations[lang], ensure_ascii=False, indent=4)
            block = "\n".join("    " + line for line in block.splitlines())
            comma = "," if i < len(langs) - 1 else ""
            f.write(f'    "{lang}": {block}{comma}\n')
        f.write("}\n")


def main():
    parser = argparse.ArgumentParser(description="Sync translation keys from code into translations.json")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--prune", action="store_true")
    parser.add_argument("--placeholder", default="humanized", choices=("humanized", "key", "empty"))
    args = parser.parse_args()

    used = find_keys(SRC)
    translations = json.loads(TRANSLATIONS.read_text(encoding="utf-8"))
    have = flat_keys(translations["en"])

    new_keys = sorted(used - have)
    old_keys = sorted(have - used) if args.prune else []

    if not new_keys and not old_keys:
        print("translations.json is up to date.")
        return 0

    for key in new_keys:
        if args.placeholder == "key":
            value = key
        elif args.placeholder == "empty":
            value = ""
        else:
            value = guess_english(key)
        print(f"  + {key} = {value!r}")
        put(translations["en"], key, value, overwrite=True)
        for lang, tree in translations.items():
            if lang != "en":
                put(tree, key, value)

    backfilled = 0
    for key in sorted(flat_keys(translations["en"])):
        parts = key.split(".")
        english = lookup(translations["en"], parts)[parts[-1]]
        for lang, tree in translations.items():
            if lang != "en" and put(tree, key, english):
                backfilled += 1
                print(f"  + [{lang}] {key}")

    for key in old_keys:
        parts = key.split(".")
        print(f"  - {key} (unused)")
        for tree in translations.values():
            node = lookup(tree, parts)
            if node is not None:
                node.pop(parts[-1], None)

    if args.check or args.dry_run:
        print(f"{len(new_keys)} new, {backfilled} backfilled, {len(old_keys)} pruned.")
        return 1 if args.check and (new_keys or backfilled or old_keys) else 0

    save(TRANSLATIONS, translations)
    print(f"Wrote {TRANSLATIONS} ({len(new_keys)} new, {backfilled} backfilled, {len(old_keys)} pruned).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
