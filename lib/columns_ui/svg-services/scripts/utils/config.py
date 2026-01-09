import tomllib

from utils.constants import BUILD_CONFIG_FILE_NAME
from utils.path import get_root_dir


def get_build_config():
    root_dir = get_root_dir()

    with open(root_dir / BUILD_CONFIG_FILE_NAME, "rb") as file:
        return tomllib.load(file)
