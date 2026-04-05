from __future__ import annotations

import argparse
import ctypes
import os
import platform
import subprocess
import sys
from pathlib import Path

from common import MSYS2_UCRT64_BIN, build_dir_for_preset, default_preset

BLUE = "\033[94m"
YELLOW = "\033[93m"
RED = "\033[91m"
RESET = "\033[0m"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run the CuteXMPP executable.")
    parser.add_argument("--preset", help="Explicit preset to run from.")
    parser.add_argument(
        "--build-type",
        default="debug",
        choices=("debug", "release"),
        help="Build type when --preset is not supplied.",
    )
    parser.add_argument(
        "app_args",
        nargs=argparse.REMAINDER,
        help="Additional arguments passed through to the CuteXMPP executable.",
    )
    return parser.parse_args()


def executable_path(build_dir: Path, preset: str) -> Path:
    system = platform.system().lower()
    if system.startswith("win"):
        return build_dir / "CuteXMPP.exe"
    if system == "darwin":
        app_bundle = build_dir / "CuteXMPP.app" / "Contents" / "MacOS" / "CuteXMPP"
        if app_bundle.exists():
            return app_bundle
    return build_dir / "CuteXMPP"


def enable_ansi_colors() -> None:
    if platform.system().lower().startswith("win"):
        kernel32 = ctypes.windll.kernel32
        handle = kernel32.GetStdHandle(-11)
        mode = ctypes.c_uint32()
        if kernel32.GetConsoleMode(handle, ctypes.byref(mode)):
            kernel32.SetConsoleMode(handle, mode.value | 0x0004)


def colorize(line: str) -> str:
    if line.startswith("[INFO]"):
        return f"{BLUE}{line}{RESET}"
    if line.startswith("[DEBUG]"):
        return f"{YELLOW}{line}{RESET}"
    if line.startswith("[ERROR]"):
        return f"{RED}{line}{RESET}"
    return line


def main() -> None:
    enable_ansi_colors()
    args = parse_args()
    preset = args.preset or default_preset(args.build_type)
    binary = executable_path(build_dir_for_preset(preset), preset)
    if not binary.exists():
        raise SystemExit(
            f"Executable not found at {binary}. Run scripts/configure.py and scripts/build.py first."
        )

    env = os.environ.copy()
    if platform.system().lower().startswith("win") and MSYS2_UCRT64_BIN.exists():
        env["PATH"] = f"{MSYS2_UCRT64_BIN};{env.get('PATH', '')}"
    env["QT_FORCE_STDERR_LOGGING"] = "1"

    print(colorize(f"[INFO] Launching {binary}"))
    forwarded_args = list(args.app_args)
    if forwarded_args and forwarded_args[0] == "--":
        forwarded_args = forwarded_args[1:]

    process = subprocess.Popen(
        [str(binary), *forwarded_args],
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        bufsize=1,
    )

    assert process.stdout is not None
    try:
        for line in process.stdout:
            print(colorize(line.rstrip()))
    except KeyboardInterrupt:
        print(colorize("[INFO] Interrupted by user, terminating CuteXMPP."))
        process.terminate()
    finally:
        return_code = process.wait()

    if return_code != 0:
        raise SystemExit(return_code)


if __name__ == "__main__":
    main()
