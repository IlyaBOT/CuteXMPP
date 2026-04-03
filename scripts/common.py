from __future__ import annotations

import platform
import subprocess
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MSYS2_UCRT64_BIN = Path("C:/msys64/ucrt64/bin")


def detect_platform() -> str:
    system = platform.system().lower()
    if system.startswith("win"):
        return "windows"
    if system == "darwin":
        return "macos"
    return "linux"


def default_preset(build_type: str) -> str:
    return f"{detect_platform()}-{build_type.lower()}"


def build_dir_for_preset(preset: str) -> Path:
    return ROOT / "build" / preset


def run_command(command: list[str]) -> None:
    env = os.environ.copy()
    if detect_platform() == "windows" and MSYS2_UCRT64_BIN.exists():
        env["PATH"] = f"{MSYS2_UCRT64_BIN};{env.get('PATH', '')}"
    subprocess.run(command, cwd=ROOT, check=True, env=env)
