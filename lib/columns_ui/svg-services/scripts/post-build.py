import argparse
import os
from pathlib import PurePath

from utils.config import get_build_config
from utils.licence import generate_licence_text
from utils.path import get_root_dir
from utils.version import get_version
from utils.zip import write_zip

parser = argparse.ArgumentParser()
parser.add_argument("component_path", type=str)
parser.add_argument("output_dir", type=str)
parser.add_argument("platform", type=str, choices=["Win32", "x64", "ARM64"])


PLATFORM_MAPPING = {
    "Win32": "x86",
    "x64": "x64",
    "ARM64": "arm64",
}


def main():
    args = parser.parse_args()
    version = get_version()
    root_dir = get_root_dir()
    build_config = get_build_config()

    component_path = PurePath(args.component_path)
    input_base_name = component_path.stem
    annotation = f"-{version}" if os.getenv("CI") else ""
    platform = PLATFORM_MAPPING[args.platform]
    output_path = (
        f"{args.output_dir}{input_base_name}{annotation}.{platform}.fb2k-component"
    )

    licences = [
        (licence["title"], root_dir / licence["path"])
        for licence in build_config.get("licences", [])
    ]
    memory_files = {
        "LICENSE.txt": generate_licence_text(licences),
    }

    write_zip(
        output_path, [(component_path, component_path.name)], memory_files=memory_files
    )

    print(f"Archive created: {output_path}")


if __name__ == "__main__":
    main()
