import re
import sys
from pathlib import Path

SDK = (Path(__file__).resolve().parent / ".." / ".." / "thirdparty" / "LExSDKv2" / "Src" / "LESDK").resolve()
COMMENT = re.compile(r"// 0x([0-9A-Fa-f]+) \(0x([0-9A-Fa-f]+)\)")
PAD = re.compile(r"^\tunsigned char SAS_Pad_0x([0-9A-Fa-f]+)\[0x([0-9A-Fa-f]+)\];")
FLAGS = re.compile(r"^\t[^\r\n]*->FunctionFlags \|=( ~0x400;\r?\n)", re.MULTILINE)
ALIGN1 = re.compile(r"(^|[^A-Za-z_])(unsigned char|char|bool|byte)([^A-Za-z_]|$)")
ALIGN2 = re.compile(r"(^|[^A-Za-z_])(unsigned short|short|word)([^A-Za-z_]|$)")


def decl_align(decl: str) -> int:
    if "union {" in decl or "*" in decl or decl.startswith(("FString", "TArray<")):
        return 4
    if ALIGN1.search(decl):
        return 1
    if ALIGN2.search(decl):
        return 2
    return 4


def fix_structs(path: Path, apply: bool) -> int:
    text = path.read_text(encoding="utf-8")
    nl = "\r\n" if "\r\n" in text else "\n"
    out, cur, holes = [], 0, 0
    for i, line in enumerate(text.splitlines(keepends=True), 1):
        pad = PAD.match(line)
        comment = COMMENT.search(line)
        if pad:
            cur = int(pad.group(1), 16) + int(pad.group(2), 16)
        elif comment and line.startswith("\t") and not line.startswith("\t//"):
            off, size = int(comment.group(1), 16), int(comment.group(2), 16)
            a = decl_align(line[: line.index("// 0x")].strip())
            if -(-cur // a) * a < off:
                out.append(f"\tunsigned char SAS_Pad_0x{cur:X}[0x{off - cur:X}];{nl}")
                print(f"[hole] {path.relative_to(SDK)}:{i}: {off - cur} byte(s) before 0x{off:X}")
                holes += 1
            cur = off + size
        out.append(line)
    if apply and holes:
        path.write_bytes("".join(out).encode("utf-8"))
    return holes


def fix_flags(path: Path, apply: bool) -> int:
    text = path.read_text(encoding="utf-8")
    fixed, n = FLAGS.subn("", text)
    if apply and n:
        path.write_bytes(fixed.encode("utf-8"))
    return n


def main() -> None:
    apply = "--apply" in sys.argv
    holes = sum(fix_structs(f, apply) for f in sorted(SDK.glob("LE[123]/*_f_structs.hpp")))
    flags = sum(fix_flags(f, apply) for f in sorted(SDK.glob("LE[123]/*_functions.cpp")))
    print(f"{holes} pad(s), {flags} flag line(s) [{'applied' if apply else 'report only, use --apply'}]")


if __name__ == "__main__":
    main()
