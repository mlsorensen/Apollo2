# Shared PlatformIO hook (used by both the device and sim envs).
#
# LVGL 9.5 globs in lv_blend_helium.S — ARM Cortex-M Helium/MVE assembly. It
# only assembles on ARM Cortex-M; on Xtensa (ESP32) and on the x86/arm64 host
# the assembler chokes on the C headers it includes. We don't use it
# (LV_USE_DRAW_SW_ASM = NONE → portable C blender), so drop it from the build.
Import("env")  # noqa: F821  (injected by PlatformIO/SCons)


def _skip(node):
    return None


env.AddBuildMiddleware(_skip, "*lv_blend_helium*")
