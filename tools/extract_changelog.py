#!/usr/bin/env python3
"""Extract one unprefixed semantic version section from a Markdown changelog."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


VERSION = re.compile(r"[0-9]+\.[0-9]+\.[0-9]+")
HEADING = re.compile(r"^##\s+([^\s]+)\s*$", re.MULTILINE)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--changelog", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def extract(content: str, version: str) -> str:
    if not VERSION.fullmatch(version):
        raise ValueError("release version must use x.y.z without a v prefix")
    headings = list(HEADING.finditer(content))
    matches = [index for index, heading in enumerate(headings) if heading.group(1) == version]
    if len(matches) != 1:
        raise ValueError(f"CHANGELOG must contain exactly one '## {version}' heading")
    index = matches[0]
    begin = headings[index].end()
    end = headings[index + 1].start() if index + 1 < len(headings) else len(content)
    body = content[begin:end].strip()
    if not body:
        raise ValueError(f"CHANGELOG section {version} has no release notes")
    return body + "\n"


def main() -> int:
    args = parse_args()
    try:
        source_root = Path(__file__).resolve().parent.parent
        output = args.output.resolve()
        try:
            output.relative_to(source_root / "build")
        except ValueError as error:
            raise ValueError(
                f"release notes output must remain below {source_root / 'build'}: {output}"
            ) from error
        notes = extract(args.changelog.read_text(encoding="utf-8"), args.version)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(notes, encoding="utf-8")
    except (OSError, ValueError) as error:
        print(f"changelog extraction failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
