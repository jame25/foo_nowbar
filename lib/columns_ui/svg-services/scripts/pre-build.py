from datetime import date

from utils.version import get_version


def main():
    version = get_version()

    with open("version.h.template", encoding="utf-8") as file:
        template = file.read()

    today = date.today()

    contents_to_write = (
        template.replace(
            "${{ Date }}",
            today.strftime("%#d %B %Y"),
        )
        .replace(
            "${{ Year }}",
            str(today.year),
        )
        .replace(
            "${{ Version }}",
            str(version),
        )
    )

    with open("version.h", "w", encoding="utf-8") as file:
        file.write(contents_to_write)


if __name__ == "__main__":
    main()
