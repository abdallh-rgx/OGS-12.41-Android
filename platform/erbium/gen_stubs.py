#!/usr/bin/env python3
"""Generate Finders-Android-stubs.cpp for Find_* declared but not defined."""
import re

hdr = open('Erbium/Erbium/Public/Finders.h').read()
src = open('android/erbium/Finders-Android.cpp').read()

decls = re.findall(r'^\s*(uint64|uint32|int32|void|bool)\s+(Find\w+)\s*\(', hdr, re.M)
declared = {name for _, name in decls}
defined = set(re.findall(r'^\s*(?:uint64_t|uint32_t|int32_t|void|bool)\s+(Find\w+)\s*\(', src, re.M))
missing = sorted(declared - defined - {'FindNullsAndRetTrues'})
print("declared:", len(declared), "defined:", len(defined), "missing:", len(missing))
print("MISSING:", missing)

sigs = {}
for m in re.finditer(r'^\s*(uint64|uint32|int32|void|bool)\s+(Find\w+)\s*\(([^)]*)\)', hdr, re.M):
    sigs[m.group(2)] = (m.group(1), m.group(3))

out = []
out.append('// ============================================================================')
out.append('// Finders-Android-stubs.cpp — Find_* declared in Finders.h that are not in')
out.append('// the v6 catalog (Windows-only pattern targets or covered by other chains).')
out.append('// They try sensible RT string chains and fall back to 0 (caller skips).')
out.append('// ============================================================================')
out.append('#include "pch-android.h"')
out.append('#include "RT.h"')
out.append('#include "../../Erbium/Erbium/Public/Finders.h"')
out.append('')
out.append('// The engine module base — set by the entry bootstrap (entry.cpp).')
out.append('uint64_t ImageBase = 0;')
out.append('')

CHAIN_HINTS = {
    'FindEncryptionPatch': ['PacketHandler', 'Encryption'],
    'FindSetIsDoorOpen': ['IsDoorOpen'],
    'FindCanPlaceBuildableClassInStructuralGrid': ['CanPlaceBuildableClassInStructuralGrid'],
}

for name in missing:
    ret, params = sigs[name]
    ret_t = ret + '_t' if ret != 'void' else 'void'
    hints = CHAIN_HINTS.get(name)
    if hints:
        arr = ', '.join(f'L"{h}"' for h in hints)
        body = (
            '    static uint64_t Result = 0;\n'
            '    static bool bInit = false;\n'
            '    if (!bInit)\n'
            '    {\n'
            '        bInit = true;\n'
            f'        const wchar_t* _ss[] = {{ {arr} }};\n'
            '        for (auto _s : _ss)\n'
            '        {\n'
            '            Result = RT::StringAnchorFunc(_s);\n'
            '            if (Result) break;\n'
            '        }\n'
            '    }\n'
            '    return Result;'
        )
    else:
        body = '    return 0; // not needed on this path (Windows-only pattern target)'
    out.append(f'{ret_t} {name}({params})')
    out.append('{')
    out.append(body)
    out.append('}')
    out.append('')

open('android/erbium/Finders-Android-stubs.cpp', 'w').write('\n'.join(out))
print("stubs written")
