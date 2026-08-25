import re
import sys
from pathlib import Path

SDK = (Path(__file__).resolve().parent / ".." / ".." / "thirdparty" / "LExSDKv2" / "Src" / "LESDK").resolve()
COMMENT = re.compile(r"// 0x([0-9A-Fa-f]+) \(0x([0-9A-Fa-f]+)\)")
PAD = re.compile(r"^(?:\t|    )unsigned char SAS_Pad_0x([0-9A-Fa-f]+)\[0x([0-9A-Fa-f]+)\];")
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
    dirty = False
    for i, line in enumerate(text.splitlines(keepends=True), 1):
        pad = PAD.match(line)
        comment = COMMENT.search(line)
        if pad:
            cur = int(pad.group(1), 16) + int(pad.group(2), 16)
            normalized = f"    unsigned char SAS_Pad_0x{pad.group(1)}[0x{pad.group(2)}];{nl}"
            if line != normalized:
                dirty = True
                line = normalized
        elif comment and line.startswith("\t") and not line.startswith("\t//"):
            off, size = int(comment.group(1), 16), int(comment.group(2), 16)
            a = decl_align(line[: line.index("// 0x")].strip())
            if -(-cur // a) * a < off:
                out.append(f"    unsigned char SAS_Pad_0x{cur:X}[0x{off - cur:X}];{nl}")
                print(f"[hole] {path.relative_to(SDK)}:{i}: {off - cur} byte(s) before 0x{off:X}")
                holes += 1
            cur = off + size
        out.append(line)
    if apply and (holes or dirty):
        path.write_bytes("".join(out).encode("utf-8"))
    return holes


def fix_flags(path: Path, apply: bool) -> int:
    text = path.read_text(encoding="utf-8")
    fixed, n = FLAGS.subn("", text)
    if apply and n:
        path.write_bytes(fixed.encode("utf-8"))
    return n


QUAT_BODY = re.compile(
    r"struct FQuat UObject::QuatFromRotator \( struct FRotator const& A \)\n"
    r"\{\n"
    r"\tstatic UFunction\* pFnQuatFromRotator = NULL;\n"
    r"\n"
    r"\tif \( ! pFnQuatFromRotator \)\n"
    r"\t\tpFnQuatFromRotator = UObject::FindObject< UFunction > \( L\"Function Core\.Object\.QuatFromRotator\" \);\n"
    r"\n"
    r"\tUObject_execQuatFromRotator_Parms QuatFromRotator_Parms;\n"
    r"\tmemcpy \( &QuatFromRotator_Parms\.A, &A, 0xC \);\n"
    r"\s*"
    r"(?:\tpFnQuatFromRotator->FunctionFlags \|= ~0x400;\s*)?"
    r"\tthis->ProcessEvent \( pFnQuatFromRotator, &QuatFromRotator_Parms, NULL \);\n"
    r"\n"
    r"\tpFnQuatFromRotator->FunctionFlags \|= 0x400;\n"
    r"\n"
    r"\treturn QuatFromRotator_Parms\.ReturnValue;\n"
    r"\};",
    re.MULTILINE,
)

QUAT_FIX = """struct FQuat UObject::QuatFromRotator ( struct FRotator const& A )
{
    const float scale = 6.2831853071795862f / 65536.f;
    const float sr = sinf(A.Roll * scale), sp = sinf(A.Pitch * scale), sy = sinf(A.Yaw * scale);
    const float cr = cosf(A.Roll * scale), cp = cosf(A.Pitch * scale), cy = cosf(A.Yaw * scale);
    float m[3][3];
    m[0][0] = cp * cy;            m[0][1] = cp * sy;            m[0][2] = sp;
    m[1][0] = sr * sp * cy - cr * sy;
    m[1][1] = sr * sp * sy + cr * cy;
    m[1][2] = -sr * cp;
    m[2][0] = -(cr * sp * cy + sr * sy);
    m[2][1] = cy * sr - cr * sp * sy;
    m[2][2] = cr * cp;
    FQuat q;
    const float tr = m[0][0] + m[1][1] + m[2][2];
    if (tr > 0.0f) {
        const float invS = 1.0f / sqrtf(tr + 1.0f);
        const float s = 0.5f * invS;
        q.W = 0.5f / invS;
        q.X = (m[1][2] - m[2][1]) * s;
        q.Y = (m[2][0] - m[0][2]) * s;
        q.Z = (m[0][1] - m[1][0]) * s;
    } else {
        int i = 0;
        if (m[1][1] > m[0][0]) i = 1;
        if (m[2][2] > m[i][i]) i = 2;
        static const int nxt[3] = { 1, 2, 0 };
        const int j = nxt[i];
        const int k = nxt[j];
        float s = m[i][i] - m[j][j] - m[k][k] + 1.0f;
        const float invS = 1.0f / sqrtf(s);
        float qt[4];
        qt[i] = 0.5f / invS;
        s = 0.5f * invS;
        qt[3] = (m[j][k] - m[k][j]) * s;
        qt[j] = (m[i][j] + m[j][i]) * s;
        qt[k] = (m[i][k] + m[k][i]) * s;
        q.X = qt[0]; q.Y = qt[1]; q.Z = qt[2]; q.W = qt[3];
    }
    return q;
};"""


def fix_quat(path: Path, apply: bool) -> int:
    text = path.read_text(encoding="utf-8")
    fixed, n = QUAT_BODY.subn(QUAT_FIX, text)
    if apply and n:
        path.write_bytes(fixed.encode("utf-8"))
    return n


def main() -> None:
    apply = "--apply" in sys.argv
    holes = sum(fix_structs(f, apply) for f in sorted(SDK.glob("LE[123]/*_f_structs.hpp")))
    flags = sum(fix_flags(f, apply) for f in sorted(SDK.glob("LE[123]/*_functions.cpp")))
    quats = sum(fix_quat(f, apply) for f in sorted(SDK.glob("LE[123]/Core_functions.cpp")))
    print(f"{holes} pad(s), {flags} flag line(s), {quats} quat fix(es) [{'applied' if apply else 'report only, use --apply'}]")


if __name__ == "__main__":
    main()
