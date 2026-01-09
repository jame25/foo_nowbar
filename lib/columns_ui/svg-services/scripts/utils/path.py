from functools import cache
from pathlib import Path

from utils.constants import BUILD_CONFIG_FILE_NAME


@cache
def get_root_dir():
    parent_dir = Path(__file__).parents[1]
    root_dir = parent_dir
    while not (root_dir / BUILD_CONFIG_FILE_NAME).exists():
        root_dir = root_dir.parent

        if not root_dir:
            raise FileNotFoundError(
                f"{BUILD_CONFIG_FILE_NAME} not found in any parent directories."
            )

    return root_dir
