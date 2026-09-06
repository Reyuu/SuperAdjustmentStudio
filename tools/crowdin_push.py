#!/usr/bin/env python3

import argparse
import json
import re
import sys
from pathlib import Path
from urllib.request import Request, urlopen
from urllib.error import HTTPError

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent
ENV_BAT = REPO_ROOT / "env.bat"
TRANSLATIONS_JSON = REPO_ROOT / "src" / "resources" / "translations.json"

DEFAULT_PROJECT_ID = 926689
DEFAULT_FILE_ID = 10
CROWDIN_API = "https://api.crowdin.com/api/v2"


def load_env_bat() -> dict[str, str]:
    if not ENV_BAT.is_file():
        return {}
    values: dict[str, str] = {}
    for line in ENV_BAT.read_text(encoding="utf-8").splitlines():
        m = re.match(r'\s*set\s+"?([A-Za-z_][A-Za-z0-9_]*)\s*=\s*"?([^"\r\n]*)', line)
        if m:
            values[m.group(1)] = m.group(2).rstrip()
    return values


def api_request(method: str, path: str, token: str, body: dict | None = None,
                raw_body: bytes | None = None, headers: dict[str, str] | None = None) -> dict:
    url = f"{CROWDIN_API}{path}"
    data = raw_body if raw_body is not None else (json.dumps(body).encode() if body else None)
    req = Request(url, data=data, method=method)
    req.add_header("Authorization", f"Bearer {token}")
    req.add_header("Accept", "application/json")
    if headers:
        for k, v in headers.items():
            req.add_header(k, v)
    elif raw_body is None:
        req.add_header("Content-Type", "application/json")
    try:
        with urlopen(req) as resp:
            return json.loads(resp.read())
    except HTTPError as e:
        err_body = e.read().decode(errors="replace")
        raise SystemExit(f"API error {e.code} {method} {path}:\n{err_body}") from e


def main() -> int:
    parser = argparse.ArgumentParser(description="Push English source strings to Crowdin")
    parser.add_argument("--token", help="Crowdin personal token (overrides env.bat)")
    parser.add_argument("--project-id", type=int, default=DEFAULT_PROJECT_ID,
                        help=f"Crowdin project ID (default: {DEFAULT_PROJECT_ID})")
    parser.add_argument("--file-id", type=int, default=DEFAULT_FILE_ID,
                        help=f"Crowdin file ID (default: {DEFAULT_FILE_ID})")
    parser.add_argument("--dry-run", action="store_true",
                        help="Print API calls, skip actual upload")
    args = parser.parse_args()

    token = args.token
    if not token:
        env = load_env_bat()
        token = env.get("CROWDIN_PERSONAL_TOKEN")
    if not token:
        raise SystemExit(
            "No Crowdin token. Pass --token or set CROWDIN_PERSONAL_TOKEN in env.bat"
        )

    print(f"Project: {args.project_id}, File ID: {args.file_id}")

    # Read translations.json and extract only "en"
    print(f"Reading {TRANSLATIONS_JSON}...")
    with open(TRANSLATIONS_JSON, encoding="utf-8") as f:
        translations = json.load(f)

    if "en" not in translations:
        raise SystemExit("No \"en\" key found in translations.json")

    en = translations["en"]
    en_content = json.dumps(en, ensure_ascii=False, indent=4) + "\n"
    print(f"English strings: {len(en)} top-level keys")

    # Upload to storage
    storage_path = "/storages"
    storage_headers = {
        "Content-Type": "application/json",
        "Crowdin-API-FileName": "translations.json",
    }
    if args.dry_run:
        print(f"[dry-run] POST {storage_path} ({len(en_content)} bytes)")
        storage_id = 0
    else:
        result = api_request("POST", storage_path, token, raw_body=en_content.encode(),
                             headers=storage_headers)
        storage_id = result["data"]["id"]
        print(f"Uploaded to storage: id={storage_id}")

    # Update file in project
    file_path = f"/projects/{args.project_id}/files/{args.file_id}"
    file_body = {
        "storageId": storage_id,
        "updateOption": "keep_translations_and_approvals",
    }
    if args.dry_run:
        print(f"[dry-run] PUT {file_path}")
    else:
        api_request("PUT", file_path, token, body=file_body)
        print(f"Updated file {args.file_id} in project {args.project_id}")

    print("Done.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
