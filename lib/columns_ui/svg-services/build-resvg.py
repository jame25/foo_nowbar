#!/usr/bin/env python3.11
import argparse
import os
import subprocess
from functools import cache
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("configuration", type=str.lower, choices=["debug", "release"])
parser.add_argument("platform", type=str.lower, choices=["win32", "x64"])


BUILD_CONFIG_FILE_NAME = "build-config.toml"


PLATFORM_TO_TOOLCHAIN_MAP = {
    "win32": "stable-i686-pc-windows-msvc",
    "x64": "stable-x86_64-pc-windows-msvc",
}


@cache
def get_root_dir():
    parent_dir = Path(__file__).parent
    root_dir = parent_dir
    while not (root_dir / BUILD_CONFIG_FILE_NAME).exists():
        root_dir = root_dir.parent

        if not root_dir:
            raise FileNotFoundError(
                f"{BUILD_CONFIG_FILE_NAME} not found in any parent directories."
            )

    return root_dir


def build(configuration, platform):
    toolchain = PLATFORM_TO_TOOLCHAIN_MAP[platform]

    subprocess.run(
        [
            "cargo",
            f"+{toolchain}",
            "build",
            "--manifest-path", r"resvg\crates\c-api\Cargo.toml",
            *(["--release"] if configuration == "release" else []),
            "--target-dir",
            rf"resvg\crates\c-api\{platform}"
        ],
        check=True,
    )


def main():
    args = parser.parse_args()
    build(args.configuration, args.platform)


if __name__ == "__main__":
    main()
