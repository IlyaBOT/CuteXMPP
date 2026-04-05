from __future__ import annotations

import platform
import subprocess
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MSYS2_UCRT64_BIN = Path("C:/msys64/ucrt64/bin")
LOCAL_QXMPP_PREFIX = ROOT / ".deps" / "qxmpp" / "install"


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


def prepare_env() -> dict[str, str]:
    env = os.environ.copy()
    if detect_platform() == "windows" and MSYS2_UCRT64_BIN.exists():
        env["PATH"] = f"{MSYS2_UCRT64_BIN};{env.get('PATH', '')}"
    if LOCAL_QXMPP_PREFIX.exists():
        prefix_path = env.get("CMAKE_PREFIX_PATH", "")
        env["CMAKE_PREFIX_PATH"] = (
            f"{LOCAL_QXMPP_PREFIX}{os.pathsep}{prefix_path}"
            if prefix_path
            else str(LOCAL_QXMPP_PREFIX)
        )
    return env


def qxmpp_available(env: dict[str, str] | None = None) -> bool:
    prefixes: list[Path] = []
    if env is None:
        env = prepare_env()

    prefix_path = env.get("CMAKE_PREFIX_PATH", "")
    if prefix_path:
        prefixes.extend(Path(entry) for entry in prefix_path.split(os.pathsep) if entry)

    prefixes.extend(Path(path) for path in ("/usr", "/usr/local", "/opt/homebrew", "/opt/local"))

    config_suffixes = (
        Path("lib/cmake/QXmpp/QXmppConfig.cmake"),
        Path("lib64/cmake/QXmpp/QXmppConfig.cmake"),
        Path("lib/cmake/QXmppQt6/QXmppQt6Config.cmake"),
        Path("lib64/cmake/QXmppQt6/QXmppQt6Config.cmake"),
    )

    seen: set[Path] = set()
    for prefix in prefixes:
        if prefix in seen:
            continue
        seen.add(prefix)
        for suffix in config_suffixes:
            if (prefix / suffix).exists():
                return True
    return False


def bootstrap_qxmpp() -> None:
    subprocess.run(["bash", "scripts/ci/build_qxmpp.sh"], cwd=ROOT, check=True, env=prepare_env())


def run_command(command: list[str]) -> None:
    subprocess.run(command, cwd=ROOT, check=True, env=prepare_env())
