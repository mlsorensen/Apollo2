# ESP32-P4 only: link the app for a 256 KB L2 cache, whatever the core's libs say.
#
# Arduino core 3.3.12 (esp32-arduino-lib-builder #395) put the P4's L2 cache
# back to IDF's default 128 KB, which moves the app's internal-RAM image down by
# 128 KB (rev v3: .iram0.text 0x4ff40000 -> 0x4ff20000; rev v1.x: the top of
# sram_high grows into 0x4ffc0000-0x4ffe0000). An OTA update only replaces the
# app — every fielded unit keeps the bootloader it was web-flashed with, built
# on <= 3.3.11 for a 256 KB cache — and that bootloader maps those addresses as
# cache, so a 3.3.12-layout app loads into the cache and dies at entry
# (Illegal instruction, then rollback: v1.1.0-beta.2, issue #4).
#
# Linking at the 256 KB layout keeps the app clear of the cache under EITHER
# bootloader (old 256 KB or new 128 KB), so OTA works whichever one a unit has.
# The libs' own code still runs with CONFIG_CACHE_L2_CACHE_SIZE = 128 KB; what
# that means for the heap at runtime is checked on the bench, not assumed.
#
# Mechanism: copy the framework's ld/memory.ld into the build dir with the
# cache size replaced, and put that dir FIRST on LIBPATH so `-T memory.ld`
# resolves to it. Fails the build if the expected lines aren't there, so a
# future core that changes memory.ld can't silently drop the fix.
import os
import re

Import("env")  # noqa: F821  (injected by PlatformIO/SCons)

kCacheSize = 0x40000  # 256 KB: what every pre-3.3.12 bootloader configured

# rev v3:   sram_seg (RWX) : org = 0x4FF00000 + 0x20000, len = ... + 0x20000
# rev v1.x: sram_high (RW) : org = 0x4FF40000, len = 0x80000 - 0x20000
_PATTERNS = [
    re.compile(r"(sram_seg \(RWX\) : org = 0x4FF00000 \+ )(0x[0-9A-Fa-f]+)"
               r"(, len = [^\n]*\+ )(0x[0-9A-Fa-f]+)"),
    re.compile(r"(sram_high \(RW\) : org = 0x4FF40000, len = 0x80000 - )"
               r"(0x[0-9A-Fa-f]+)"),
]


def _patch(text):
    hits = 0
    for pat in _PATTERNS:
        def repl(m):
            groups = list(m.groups())
            for i in range(1, len(groups), 2):  # every 2nd group is a size
                groups[i] = hex(kCacheSize)
            return "".join(groups)
        text, n = pat.subn(repl, text)
        hits += n
    return text, hits


def _setup():
    if env.BoardConfig().get("build.mcu", "") != "esp32p4":  # noqa: F821
        return
    src = None
    for d in env.get("LIBPATH", []):  # noqa: F821
        cand = os.path.join(env.subst(str(d)), "memory.ld")  # noqa: F821
        if os.path.isfile(cand):
            src = cand
            break
    if src is None:
        env.Exit("p4_l2_compat: framework memory.ld not found on LIBPATH")  # noqa: F821
    with open(src) as f:
        text, hits = _patch(f.read())
    if hits != 1:
        env.Exit("p4_l2_compat: expected 1 L2-cache line in %s, found %d — "
                 "re-check the layout before shipping (issue #4)" % (src, hits))  # noqa: F821
    out_dir = os.path.join(env.subst("$BUILD_DIR"), "ld_l2_256k")  # noqa: F821
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "memory.ld"), "w") as f:
        f.write(text)
    env.Prepend(LIBPATH=[out_dir])  # noqa: F821
    print("p4_l2_compat: linking for a 256 KB L2 cache (%s)" % src)


_setup()
