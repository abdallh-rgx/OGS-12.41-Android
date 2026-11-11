#!/usr/bin/env python3
"""Bake SDK bootstrap headers for the Erbium Android port.
Input: sdk-bootstrap-final.json (mining) + final_resolved_v4.json (v6 game funcs)
Output: android/Generated/sdk-<ver>.h for each of the 11 versions + sdk-versions.cmake
"""
import json, os, sys

OUT = '/home/z/android-port/fork/android/Generated'
MINING = '/home/z/android-port/mining/sdk-bootstrap-final.json'
V6 = '/home/z/my-project/erbium-finders/final_resolved_v4.json'

# version label → (fortnite_version, lib name, engine)
VERSIONS = {
    'v5.2.0':  ('5.41',  'libUE4.so',    '4.19'),  # family proxy
    'v7.40':   ('7.40',  'libUE4.so',    '4.20'),
    'v10.40':  ('9.41',  'libUE4.so',    '4.23'),  # family proxy (Erbium ref CL!)
    'v13.40':  ('13.40', 'libUE4.so',    '4.26'),
    'v17.50':  ('17.50', 'libUE4.so',    '4.27'),
    'v18.40':  ('18.40', 'libUE4.so',    '5.0'),
    'v19.10':  ('19.10', 'libUE4.so',    '5.0'),
    'v21.30':  ('21.30', 'libUnreal.so', '5.0'),
    'v22.40':  ('22.40', 'libUnreal.so', '5.1'),
    'v23.50':  ('23.50', 'libUnreal.so', '5.3'),
    'v32.00':  ('30.00', 'libUnreal.so', '5.4'),  # family proxy
}

def hexify(x):
    if x is None: return "0"
    if isinstance(x, int): return hex(x)
    if isinstance(x, str):
        try: return hex(int(x, 16))
        except Exception: return "0"
    if isinstance(x, dict) and 'addr' in x: return hexify(x['addr'])
    return "0"

def pat(x):
    if isinstance(x, dict) and x.get('pattern'): return x['pattern']
    return None

def main():
    os.makedirs(OUT, exist_ok=True)
    mining = json.load(open(MINING))
    v6 = {}
    try:
        v6 = json.load(open(V6))
    except Exception:
        pass

    cmake_versions = []
    for key, (ver, lib, engine) in VERSIONS.items():
        m = mining.get(key, {})
        fn = m.get('functions', {})

        # gather SDK core
        fields = {}
        fields['GObjects'] = hexify(m.get('GObjects'))
        fields['GEngine'] = hexify(m.get('GEngine'))
        for name in ('ProcessEvent', 'StaticFindObject', 'StaticLoadObject', 'SpawnActor',
                     'FNameConstructor', 'AppendString', 'Realloc'):
            fields[name] = hexify(m.get(name))

        # patterns for the chain-based functions
        pats = {}
        for name in ('ProcessEvent', 'StaticFindObject', 'StaticLoadObject', 'SpawnActor',
                     'FNameConstructor', 'AppendString'):
            p = pat(fn.get(name))
            if p and fn.get(name, {}).get('unique'):
                pats[name] = p
        # Realloc pattern
        rp = m.get('Realloc', {})
        if isinstance(rp, dict) and rp.get('unique') and rp.get('pattern'):
            pats['Realloc'] = rp['pattern']

        # release string
        rel = m.get('release_string')
        fields['ReleaseString'] = hexify(rel)

        # v6 game-function baked values (fast path; the Finders also have runtime chains)
        game = {}
        for fname, data in v6.items():
            if not isinstance(data, dict):
                continue
            ent = data.get(ver)
            if isinstance(ent, dict):
                a = ent.get('addr') or ent.get('global') or ent.get('address')
                if a:
                    game[fname] = hexify(a)

        lines = []
        lines.append(f"// GENERATED from mining data — {key} (FN {ver}, UE {engine}, {lib})")
        lines.append("// Do not edit; regenerate via android/erbium/bake.py")
        lines.append("#pragma once")
        lines.append(f"#define ERBIUM_BAKED_VERSION {ver}")
        lines.append(f"#define ERBIUM_BAKED_ENGINE {engine}")
        lines.append(f"#define ERBIUM_BAKED_LIB \"{lib}\"")
        lines.append("namespace ErbiumBaked {")
        for k in sorted(fields):
            lines.append(f"    inline constexpr uint64_t k{k} = {fields[k]};")
        if pats:
            lines.append("    // verified-unique ARM64 patterns (fast path before runtime chains)")
            lines.append("    namespace Pats {")
            for k in sorted(pats):
                esc = pats[k].replace('"', '\\"')
                if len(esc) > 200:
                    esc = esc[:200]  # keep sane; long patterns still work
                lines.append(f"        inline constexpr const char* k{k} = \"{esc}\";")
            lines.append("    }")
        if game:
            lines.append("    // v6-verified game function addresses (fast path for Finders)")
            lines.append("    namespace Game {")
            for k in sorted(game):
                lines.append(f"        inline constexpr uint64_t k{k} = {game[k]};")
            lines.append("    }")
        lines.append("}")
        out = "\n".join(lines) + "\n"

        fname = f"sdk-{ver}.h"
        with open(os.path.join(OUT, fname), 'w') as f:
            f.write(out)

        # ── upstream-format offsets-<ver>.h (android/src/offsets.h + entry.cpp) ──
        gis_v6 = {}
        try:
            v6d = json.load(open(V6))
            for fn in ('FindGIsClient', 'FindGIsServer'):
                e = v6d.get(fn, {}).get(ver)
                if isinstance(e, dict):
                    gis_v6[fn] = e.get('addr') or e.get('global')
        except Exception:
            pass
        gis_client = gis_v6.get('FindGIsClient', '0')
        gis_server = gis_v6.get('FindGIsServer', '0')
        gac_render = '0'  # kGAllowCommandletRendering: adjacent to GIs (best-effort below)
        # known-good upstream values for 21.30 (validated by their CI bake)
        UPSTREAM_GT = {'21.30': dict(gac='0xdc80830', owen_pr='0x89f02f0', owen_url='0x70', owen_set='0x89edcf8')}
        gt = UPSTREAM_GT.get(ver, {})
        rel = fields.get('ReleaseString', '0')
        rel_str = f"++Fortnite+Release-{ver}-CL-0000000"  # informational only
        up = []
        up.append(f"// GENERATED (upstream Baked:: format) — {key} (FN {ver})")
        up.append("#pragma once")
        up.append("#include <stdint.h>")
        up.append("")
        up.append("namespace Erbium::Baked {")
        up.append(f'inline constexpr const char* kLibName = "{lib}";')
        up.append(f'inline constexpr const char* kRelease = "{rel_str}";')
        up.append(f"inline constexpr uint64_t kGIsClient = {gis_client};")
        up.append(f"inline constexpr uint64_t kGIsServer = {gis_server};")
        up.append(f"inline constexpr uint64_t kGAllowCommandletRendering = {gt.get('gac', '0')};")
        up.append(f"inline constexpr uint64_t kReleaseString = {rel};")
        up.append('inline constexpr const char* kConsoleCommandSymbol = "Java_com_epicgames_unreal_GameActivity_nativeConsoleCommand";')
        up.append(f"inline constexpr uint64_t kOwenProcessRequest = {gt.get('owen_pr', '0')};")
        up.append(f"inline constexpr uint64_t kOwenGetUrlField = {gt.get('owen_url', '0')};")
        up.append(f"inline constexpr uint64_t kOwenSetUrl = {gt.get('owen_set', '0')};")
        up.append(f"inline constexpr uint64_t kGEngine = {fields.get('GEngine', '0')};")
        up.append(f"inline constexpr uint64_t kNativeConsoleCommand = 0; // dlsym-resolved at runtime")
        up.append("} // namespace Erbium::Baked")
        with open(os.path.join(OUT, f"offsets-{ver}.h"), 'w') as f:
            f.write("\n".join(up) + "\n")

        cmake_versions.append(ver)
        print(f"baked {fname} + offsets-{ver}.h: {len(fields)} fields, {len(pats)} patterns, {len(game)} game addrs")

    # version list for CMake
    with open(os.path.join(OUT, 'sdk-versions.cmake'), 'w') as f:
        f.write("# generated — all baked SDK versions\nset(ERBIUM_SDK_VERSIONS " + " ".join(cmake_versions) + ")\n")
    print("\nDONE:", len(cmake_versions), "versions")

if __name__ == '__main__':
    main()
