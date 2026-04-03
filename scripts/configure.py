from __future__ import annotations

import argparse
import shutil

from common import build_dir_for_preset, default_preset, run_command


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
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    preset = args.preset or default_preset(args.build_type)
    build_dir = build_dir_for_preset(preset)

    if args.clean and build_dir.exists():
        shutil.rmtree(build_dir)

    run_command(["cmake", "--preset", preset])


if __name__ == "__main__":
    main()
