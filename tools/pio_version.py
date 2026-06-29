# Stamp the firmware's git revision into the build as FW_GIT_REV, so the on-screen
# version (Stats > Info) identifies exactly which build is flashed on a device.
Import("env")  # noqa: F821  (injected by PlatformIO/SCons)
import subprocess


def _git_rev():
    try:
        rev = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"], stderr=subprocess.DEVNULL
        ).decode().strip()
        # Mark uncommitted builds so a flashed dev image is never mistaken for the
        # committed revision. (Also catch staged-but-uncommitted changes.)
        dirty = (subprocess.call(["git", "diff", "--quiet"]) != 0 or
                 subprocess.call(["git", "diff", "--cached", "--quiet"]) != 0)
        if dirty:
            rev += "_dirty"
        return rev
    except Exception:
        return "nogit"


env.Append(CPPDEFINES=[("FW_GIT_REV", env.StringifyMacro(_git_rev()))])
