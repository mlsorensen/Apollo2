# PlatformIO build hook for the native host simulator (env:sim).
#
# Apply our C++ standard to C++ sources only. LVGL is mostly C, and the compiler
# rejects "-std=c++NN" on C files, so this can't live in build_flags.
# (The LVGL ARM-asm skip is shared, in pio_skip_lvgl_asm.py.)
Import("env")  # noqa: F821  (injected by PlatformIO/SCons)

env.Append(CXXFLAGS=["-std=c++20"])
