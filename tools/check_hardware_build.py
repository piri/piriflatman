#!/usr/bin/env python3
"""Compile the hardware adapter in an isolated, synthetic fixture. Never upload."""
import pathlib
import shutil
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
PIO = ROOT / ".venv/bin/pio"


def main():
    with tempfile.TemporaryDirectory(prefix="pfm-build-only-") as directory:
        project = pathlib.Path(directory)
        for name in ("include", "src"):
            shutil.copytree(ROOT / name, project / name)
        for name in ("platformio.ini", "config.example.h"):
            shutil.copy2(ROOT / name, project / name)
        # The unconfigured hardware profile must fail closed at compile time.
        result = subprocess.run([str(PIO), "run", "-d", str(project), "-e", "esp32-hardware"], capture_output=True, text=True)
        assert result.returncode != 0 and "Hardware profile requires verified GPIO" in result.stdout + result.stderr
        print("PASS unconfigured hardware profile rejected", flush=True)
        # These numbers are compiler fixtures, not approved hardware selections.
        (project / "config.local.h").write_text(
            '#include "config.example.h"\n#define PFM_HARDWARE_VERIFIED 1\n'
            '#define PFM_PWM_GPIO 25\n#define PFM_PWM_HZ 1000\n#define PFM_PWM_BITS 12\n')
        subprocess.run([str(PIO), "run", "-d", str(project), "-e", "esp32-hardware"], check=True)
        print("PASS hardware adapter compile-only fixture; temporary images deleted on exit")


if __name__ == "__main__":
    main()
