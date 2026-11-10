#!/usr/bin/env python3
"""SDK clang-compat fixer for the Android port.

Fixes applied (idempotent):
 1. Collect every enum-member identifier used in SDK/SDK/*.hpp that collides
    with a bionic errno macro (e.g. EM_MAX -> 102) and #undef them in
    SDK/SDK/Basic.hpp (guarded, once).
 2. 'enum class TYPE NAME;' member declarations -> 'TYPE NAME;'
    (elaborated-type-specifier with enum-class is ill-formed on clang).
 3. Insert explicit tail padding into base classes whose Itanium layout
    (tail-padding reuse) diverges from the engine's real layout — currently
    FField (+0x4) so FProperty::ArrayDim lands at 0x38 like the real binary
    (measured from real Android binaries; see Erbium-Android mining data).
"""
import re
import sys
from pathlib import Path

ROOT = Path("/home/z/my-project")
SDK = ROOT / "SDK"

BIONIC_ERRNO_MACROS = set(
    """EPERM ENOENT ESRCH EINTR EIO ENXIO E2BIG ENOEXEC EBADF ECHILD EAGAIN ENOMEM EACCES EFAULT
    ENOTBLK EBUSY EEXIST EXDEV ENODEV ENOTDIR EISDIR EINVAL ENFILE EMFILE ENOTTY ETXTBSY EFBIG
    ENOSPC ESPIPE EROFS EMLINK EPIPE EDOM ERANGE EDEADLK ENAMETOOLONG ENOLCK ENOSYS ENOTEMPTY
    ELOOP ENOMSG EIDRM EOVERFLOW EBADMSG EMULTIHOP ENODATA ENOLINK ENOSR ENOSTR EPROTO ETIME
    EOPNOTSUPP ENETDOWN ENETUNREACH ENETRESET ECONNABORTED ECONNRESET ENOBUFS EISCONN ENOTCONN
    ESHUTDOWN ETOOMANYREFS ETIMEDOUT ECONNREFUSED EHOSTDOWN EHOSTUNREACH EALREADY EINPROGRESS
    ESTALE EDQUOT ECANCELED EOWNERDEAD ENOTRECOVEREOF ERFKILL EHWPOISON""".split()
)
# bionic also defines these legacy aliases
EXTRA = {"EMAXERRNO": 0, "EMAX": 0, "__ELASTERROR": 0}
ERRNO_NAMES = BIONIC_ERRNO_MACROS | set(EXTRA)


def collect_enum_members():
    """All identifiers used as enum members inside the SDK enum bodies."""
    members = set()
    pat = re.compile(r"enum class\s+(\w+)\s*(?::\s*\w+\s*)?\{([^}]*)\}", re.S)
    for f in SDK.glob("SDK/*.hpp"):
        text = f.read_text(encoding="utf-8", errors="ignore")
        for _, body in pat.findall(text):
            for line in body.split("\n"):
                line = line.strip()
                if not line or line.startswith("//"):
                    continue
                name = line.split("=")[0].strip().rstrip(",").strip()
                if re.fullmatch(r"\w+", name):
                    members.add(name)
    return members


def fix_undef_errno(basic: str, collisions):
    marker = "// OGS-ANDROID-ERRNO-UNDEF"
    if marker in basic:
        return basic, False
    lines = sorted(collisions)
    block = ["#ifdef __ANDROID__",
             "// bionic's <errno.h> defines these; SDK enums reuse the names.",
             marker]
    block += [f"#undef {n}" for n in lines]
    block.append("#endif")
    # insert right after the includes block (before 'namespace SDK')
    anchor = "namespace SDK\n{"
    idx = basic.index(anchor)
    basic = basic[:idx] + "\n".join(block) + "\n\n" + basic[idx:]
    return basic, True


def fix_elaborated_enum_class():
    pat = re.compile(r"enum class (\w+)(\s+)(\w+);")
    changed = 0
    for f in SDK.glob("SDK/*.hpp"):
        text = f.read_text(encoding="utf-8", errors="ignore")
        new, n = pat.subn(r"\1\2\3;", text)
        if n:
            f.write_text(new, encoding="utf-8")
            changed += n
    return changed


def fix_ffield_tail(basic: str):
    marker = "Pad_ClangTail"
    if marker in basic:
        return basic, False
    # FField: add explicit 4-byte tail padding so that (Itanium) dsize == sizeof,
    # forcing FProperty::ArrayDim to 0x38 as measured on real Android binaries.
    old = """public:
        void*                                         VTable;                                            // 0x0000(0x0008)(NOT AUTO-GENERATED PROPERTY)
        class FFieldClass*                            ClassPrivate;                                      // 0x0008(0x0008)(NOT AUTO-GENERATED PROPERTY)
        FFieldVariant                                 Owner;                                             // 0x0010(0x0010)(NOT AUTO-GENERATED PROPERTY)
        class FField*                                 Next;                                              // 0x0020(0x0008)(NOT AUTO-GENERATED PROPERTY)
        FName                                         Name;                                              // 0x0028(0x0008)(NOT AUTO-GENERATED PROPERTY)
        int32                                         ObjFlags;                                          // 0x0030(0x0004)(NOT AUTO-GENERATED PROPERTY)
};"""
    new = """public:
        void*                                         VTable;                                            // 0x0000(0x0008)(NOT AUTO-GENERATED PROPERTY)
        class FFieldClass*                            ClassPrivate;                                      // 0x0008(0x0008)(NOT AUTO-GENERATED PROPERTY)
        FFieldVariant                                 Owner;                                             // 0x0010(0x0010)(NOT AUTO-GENERATED PROPERTY)
        class FField*                                 Next;                                              // 0x0020(0x0008)(NOT AUTO-GENERATED PROPERTY)
        FName                                         Name;                                              // 0x0028(0x0008)(NOT AUTO-GENERATED PROPERTY)
        int32                                         ObjFlags;                                          // 0x0030(0x0004)(NOT AUTO-GENERATED PROPERTY)
#ifdef __ANDROID__
        // Itanium ABI would reuse these 4 tail bytes for FProperty::ArrayDim;
        // the real engine binary keeps the MSVC-style layout (ArrayDim @ 0x38,
        // measured from real Android binaries — Erbium-Android mining data),
        // so force dsize(FField) == sizeof(FField) with explicit padding.
        uint8                                         Pad_ClangTail[0x4];                                // 0x0034(0x0004)(Android layout fix)
#endif
};"""
    if old not in basic:
        print("WARN: FField pattern not found", file=sys.stderr)
        return basic, False
    return basic.replace(old, new, 1), True


def main():
    members = collect_enum_members()
    collisions = members & ERRNO_NAMES
    print("enum members:", len(members), "| errno collisions:", sorted(collisions))

    basic_path = SDK / "SDK/Basic.hpp"
    basic = basic_path.read_text(encoding="utf-8")
    basic, ch1 = fix_undef_errno(basic, collisions)
    basic, ch3 = fix_ffield_tail(basic)
    if ch1 or ch3:
        basic_path.write_text(basic, encoding="utf-8")
    print(f"Basic.hpp: errno-undef={ch1} ffield-tail={ch3}")

    n = fix_elaborated_enum_class()
    print("elaborated enum-class fixed:", n)


if __name__ == "__main__":
    main()
