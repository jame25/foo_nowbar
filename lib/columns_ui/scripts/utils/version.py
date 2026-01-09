import os
import re
import subprocess
from dataclasses import dataclass
from itertools import chain

_VERSION_REGEX = re.compile(
    r"^v(?P<version>.+)-(?P<distance>\d+)-g(?P<commit>[0-9a-f]+)(-(?P<dirty>dirty))?$",
)


def get_version():
    null_version = _Version("0.0.0", 0, "unknown", False)

    try:
        describe_result = subprocess.run(
            ["git", "describe", "--tags", "--dirty", "--match", "v[0-9]*", "--long"],
            capture_output=True,
            check=True,
        )
    except subprocess.SubprocessError:
        return null_version

    match_result = _VERSION_REGEX.match(describe_result.stdout.decode("utf-8"))

    if not match_result:
        return null_version

    return _Version(
        match_result["version"],
        int(match_result["distance"]),
        match_result["commit"],
        bool(match_result["dirty"]),
    )


@dataclass(frozen=True)
class _Version:
    base_version: str
    distance: int
    commit_hash: str
    dirty: bool

    def __str__(self):
        is_github_actions = os.getenv("GITHUB_ACTIONS")
        github_run_id = os.getenv("GITHUB_RUN_ID")
        github_run_number = os.getenv("GITHUB_RUN_NUMBER")

        annotations = [
            *chain(
                [f"{self.distance}.g{self.commit_hash}"] if self.distance else [],
                ["github"] if is_github_actions else [],
                [f"r{github_run_id}.{github_run_number}"] if github_run_id else [],
                ["dirty"] if self.dirty else [],
            ),
        ]

        if annotations:
            joined_annotations = ".".join(annotations)
            return f"{self.base_version}+{joined_annotations}"

        return self.base_version

    def is_release(self):
        return self.distance == 0 and not self.dirty
