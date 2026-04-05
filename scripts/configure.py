from __future__ import annotations

import argparse
import shutil
import subprocess

from common import bootstrap_qxmpp, build_dir_for_preset, default_preset, detect_platform, qxmpp_available, run_command


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Configure CuteXMPP with CMake presets.")
    parser.add_argument("--preset", help="Explicit CMake configure preset to use.")
    parser.add_argument(
        "--build-type",
        default="debug",
        choices=("debug", "release"),
        help="Preset build type when --preset is not supplied.",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Delete the build directory for the selected preset before configuring.",
    )
    parser.add_argument(
        "--skip-bootstrap-qxmpp",
        action="store_true",
        help="Do not auto-build a local QXmpp copy when the dependency is missing.",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    preset = args.preset or default_preset(args.build_type)
    build_dir = build_dir_for_preset(preset)

    if args.clean and build_dir.exists():
        shutil.rmtree(build_dir)

    if detect_platform() != "windows" and not args.skip_bootstrap_qxmpp and not qxmpp_available():
        print("[INFO] QXmpp was not found. Bootstrapping a local copy into .deps/qxmpp/install...")
        try:
            bootstrap_qxmpp()
        except subprocess.CalledProcessError as error:
            raise SystemExit(
                "Failed to build the local QXmpp dependency. "
                "You can retry with `bash scripts/ci/build_qxmpp.sh` or configure manually via CMAKE_PREFIX_PATH."
            ) from error

    try:
        run_command(["cmake", "--preset", preset])
    except subprocess.CalledProcessError as error:
        raise SystemExit(f"CMake configure failed for preset `{preset}`.") from error


if __name__ == "__main__":
    main()
