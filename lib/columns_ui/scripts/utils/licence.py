import os
from io import StringIO
from os import linesep


def generate_licence_text(licences):
    if not licences:
        return None

    return f"{linesep * 2}".join(
        _format_licence(title, path) for title, path in licences
    )


def _format_licence(title, path):
    with open(path, encoding="utf-8") as file:
        licence_text = file.read()

    # Use StringIO to translate newlines to the OS line ending
    with StringIO(newline=os.linesep) as stream:
        entry_text = f"""{title}
{"—" * len(title)}

{licence_text}
"""

        stream.write(entry_text)
        return stream.getvalue()
