#!/usr/bin/env python3

import argparse
import io
import json
import re
import sys
import time
import zipfile
from pathlib import Path
from urllib.request import Request, urlopen
from urllib.error import HTTPError

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent
ENV_BAT = REPO_ROOT / "env.bat"
TRANSLATIONS_JSON = REPO_ROOT / "src" / "resources" / "translations.json"

DEFAULT_PROJECT_ID = 926689
CROWDIN_API = "https://api.crowdin.com/api/v2"

# Crowdin language ID -> app language code (only for codes that differ)
CODE_MAP = {}


def load_env_bat() -> dict[str, str]:
    if not ENV_BAT.is_file():
        return {}
    values: dict[str, str] = {}
    for line in ENV_BAT.read_text(encoding="utf-8").splitlines():
        m = re.match(r'\s*set\s+"?([A-Za-z_][A-Za-z0-9_]*)\s*=\s*"?([^"\r\n]*)', line)
        if m:
            values[m.group(1)] = m.group(2).rstrip()
    return values


def api_request(method: str, path: str, token: str, body: dict | None = None) -> dict:
    url = f"{CROWDIN_API}{path}"
    data = json.dumps(body).encode() if body else None
    req = Request(url, data=data, method=method)
    req.add_header("Authorization", f"Bearer {token}")
    req.add_header("Content-Type", "application/json")
    req.add_header("Accept", "application/json")
    try:
        with urlopen(req) as resp:
            return json.loads(resp.read())
    except HTTPError as e:
        err_body = e.read().decode(errors="replace")
        raise SystemExit(f"API error {e.code} {method} {path}:\n{err_body}") from e


def create_build(project_id: int, token: str, dry_run: bool) -> int:
    path = f"/projects/{project_id}/translations/builds"
    if dry_run:
        print(f"[dry-run] POST {path}")
        return 0
    result = api_request("POST", path, token)
    build_id = result["data"]["id"]
    print(f"Build created: {build_id}")
    return build_id


def poll_build(project_id: int, build_id: int, token: str, dry_run: bool) -> dict:
    path = f"/projects/{project_id}/translations/builds/{build_id}"
    start = time.time()
    while True:
        if dry_run:
            print(f"[dry-run] GET {path}")
            return {"data": {"status": "finished"}}
        result = api_request("GET", path, token)
        status = result["data"]["status"]
        print(f"  Build status: {status}")
        if status == "finished":
            return result
        if status in ("failed", "canceled"):
            error = result["data"].get("error", {}).get("message", "unknown error")
            raise SystemExit(f"Build {status}: {error}")
        if time.time() - start > 60:
            raise SystemExit("Build timed out after 60s")
        time.sleep(2)


def get_download_url(project_id: int, build_id: int, token: str, dry_run: bool) -> str:
    path = f"/projects/{project_id}/translations/builds/{build_id}/download"
    if dry_run:
        print(f"[dry-run] GET {path}")
        return ""
    result = api_request("GET", path, token)
    return result["data"]["url"]


def download_zip(url: str, dry_run: bool) -> zipfile.ZipFile | None:
    if dry_run:
        print(f"[dry-run] Download {url}")
        return None
    print("Downloading translations...")
    req = Request(url)
    with urlopen(req) as resp:
        data = resp.read()
    return zipfile.ZipFile(io.BytesIO(data))


def map_code(crowdin_code: str) -> str:
    return CODE_MAP.get(crowdin_code, crowdin_code)


def list_zip_contents(zf: zipfile.ZipFile) -> None:
    for name in zf.namelist():
        info = zf.getinfo(name)
        print(f"  {name}  ({info.file_size} bytes)")


def extract_translations(zf: zipfile.ZipFile, dry_run: bool) -> dict:
    result: dict[str, dict] = {}
    names = zf.namelist()
    if not names:
        print("Warning: ZIP is empty")
        return result

    for name in names:
        if not name.endswith(".json"):
            continue
        parts = Path(name).parts
        if len(parts) < 2:
            continue
        crowdin_lang = parts[0]
        app_code = map_code(crowdin_lang)
        print(f"  {crowdin_lang} -> {app_code}")
        if not dry_run:
            with zf.open(name) as f:
                data = json.load(f)
            # Crowdin wraps translations under an extra "en" key — unwrap it
            if "en" in data and isinstance(data["en"], dict) and len(data) == 1:
                data = data["en"]
            result[app_code] = data
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="Pull translations from Crowdin")
    parser.add_argument("--token", help="Crowdin personal token (overrides env.bat)")
    parser.add_argument("--project-id", type=int, default=DEFAULT_PROJECT_ID,
                        help=f"Crowdin project ID (default: {DEFAULT_PROJECT_ID})")
    parser.add_argument("--dry-run", action="store_true",
                        help="Print API calls and ZIP contents, skip file write")
    parser.add_argument("--list", action="store_true",
                        help="Download ZIP, print contents, then exit")
    args = parser.parse_args()

    token = args.token
    if not token:
        env = load_env_bat()
        token = env.get("CROWDIN_PERSONAL_TOKEN")
    if not token:
        raise SystemExit(
            "No Crowdin token. Pass --token or set CROWDIN_PERSONAL_TOKEN in env.bat"
        )

    project_id = args.project_id
    print(f"Project: {project_id}")

    build_id = create_build(project_id, token, args.dry_run)
    poll_build(project_id, build_id, token, args.dry_run)
    url = get_download_url(project_id, build_id, token, args.dry_run)

    zf = download_zip(url, args.dry_run)

    if zf is None:
        return 0

    print("ZIP contents:")
    list_zip_contents(zf)

    if args.list:
        return 0

    print("Languages found:")
    translations = extract_translations(zf, args.dry_run)

    if not translations:
        print("Warning: no language files found in ZIP")
        return 1

    if not args.dry_run:
        existing: dict = {}
        if TRANSLATIONS_JSON.is_file():
            with open(TRANSLATIONS_JSON, encoding="utf-8") as f:
                existing = json.load(f)

        if "en" in existing:
            translations["en"] = existing["en"]

        TRANSLATIONS_JSON.parent.mkdir(parents=True, exist_ok=True)
        with open(TRANSLATIONS_JSON, "w", encoding="utf-8") as f:
            f.write("{\n")
            keys = sorted(k for k in translations if k != "en")
            if "en" in translations:
                keys.insert(0, "en")
            for i, key in enumerate(keys):
                block = json.dumps(translations[key], ensure_ascii=False, indent=4)
                block = "\n".join("    " + line for line in block.splitlines())
                comma = "," if i < len(keys) - 1 else ""
                f.write(f'    "{key}": {block}{comma}\n')
            f.write("}\n")
        print(f"Wrote {TRANSLATIONS_JSON}")
    else:
        print(f"[dry-run] Would write {len(translations)} languages to {TRANSLATIONS_JSON}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
