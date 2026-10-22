"""Run the CI checks locally before a push. Enabled by the .githooks/pre-push hook.

Skip once with: git push --no-verify
"""

import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT / "tools"
PY = sys.executable
FW_ENV = "esp01_1m"


def run(title, cmd, cwd=ROOT):
    print(f"== {title}", flush=True)
    return subprocess.run(cmd, cwd=cwd).returncode == 0


def ldscript():
    text = (ROOT / "platformio.ini").read_text(encoding="utf-8")
    m = re.search(r"^board_build\.ldscript\s*=\s*(\S+)", text, re.MULTILINE)
    return m.group(1) if m else ""


def main():
    pio = shutil.which("pio")
    steps = []

    if pio:
        steps.append(("Unit tests (host)", [pio, "test", "-e", "native"], ROOT))
    else:
        print("!! PlatformIO not found: skipping firmware tests and build (CI still runs them)")

    steps.append(("Python tests", [PY, "-m", "pytest", "tests"], TOOLS))
    steps.append((
        "Lint tools",
        [PY, "-m", "ruff", "check", "tools", "--select", "E9,F63,F7,F82,F401,F811"],
        ROOT,
    ))
    steps.append((
        "bitmaps.h matches its PNG sources",
        [PY, "-m", "bitmaptool", "regen", "assets/manifest.json", "--check"],
        TOOLS,
    ))

    if pio:
        steps.append(("Build firmware", [pio, "run", "-e", FW_ENV], ROOT))
        steps.append((
            "Firmware leaves room for an over-the-air update",
            [PY, "tools/ota_tool.py", "check-size",
             "--in", f".pio/build/{FW_ENV}/firmware.bin", "--ldscript", ldscript()],
            ROOT,
        ))

    for title, cmd, cwd in steps:
        if not run(title, cmd, cwd):
            print(f"\nFAILED: {title}. Push cancelled (git push --no-verify skips these checks).")
            return 1

    print("\nAll checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
