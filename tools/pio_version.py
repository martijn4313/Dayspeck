"""PlatformIO pre-build script: embeds the git commit as FW_GIT_HASH."""
import subprocess

Import("env")  # noqa: F821  (provided by PlatformIO's SCons)

try:
    git_hash = subprocess.check_output(
        ["git", "rev-parse", "--short", "HEAD"], stderr=subprocess.DEVNULL
    ).decode().strip()
except Exception:
    git_hash = "unknown"

env.Append(CPPDEFINES=[("FW_GIT_HASH", env.StringifyMacro(git_hash))])  # noqa: F821
