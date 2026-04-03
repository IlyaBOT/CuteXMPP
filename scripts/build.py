from __future__ import annotations

import argparse

from common import build_dir_for_preset, default_preset, run_command


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build CuteXMPP with CMake build presets.")
    parser.add_argument("--preset", help="Explicit CMake build preset to use.")
    parser.add_argument(
        "--build-type",
        default="debug",
        choices=("debug", "release"),
        help="Build type when --preset is not supplied.",
    )
    parser.add_argument(
        "--configure",
        action="store_true",
        help="Run configure automatically before building.",
    )
    parser.add_argument("--target", help="Optional build target.")
    parser.add_argument("--jobs", type=int, help="Parallel job count.")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    preset = args.preset or default_preset(args.build_type)
    build_dir = build_dir_for_preset(preset)

    if args.configure or not build_dir.exists():
        run_command(["cmake", "--preset", preset])

    command = ["cmake", "--build", "--preset", preset]
    if args.target:
        command.extend(["--target", args.target])
    if args.jobs:
        command.extend(["--parallel", str(args.jobs)])

    run_command(command)


if __name__ == "__main__":
    main()
