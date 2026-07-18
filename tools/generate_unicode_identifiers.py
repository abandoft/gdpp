#!/usr/bin/env python3
"""Generate GDPP Unicode identifier and UTS #39 security tables."""

from __future__ import annotations

import argparse
import hashlib
import re
from pathlib import Path


ARRAY_PATTERN = re.compile(
    r"const int (?P<name>xid_(?:start|continue))_size = (?P<count>\d+);\s*"
    r"const CharRange (?P=name)\[(?P=name)_size\] = \{(?P<body>.*?)\n\};",
    re.DOTALL,
)
RANGE_PATTERN = re.compile(r"\{\s*(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+)\s*\}")
UNICODE_VERSION_PATTERN = re.compile(r"unicode\.org/Public/([^/]+)/ucd/DerivedCoreProperties\.txt")
DEFAULT_IGNORABLE_PATTERN = re.compile(
    r"^([0-9A-Fa-f]+)(?:\.\.([0-9A-Fa-f]+))?\s*;\s*Default_Ignorable_Code_Point\b"
)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--godot-char-range", type=Path, required=True)
    parser.add_argument("--confusables", type=Path, required=True)
    parser.add_argument("--unicode-data", type=Path, required=True)
    parser.add_argument("--derived-core-properties", type=Path, required=True)
    parser.add_argument("--godot-tag", default="4.7-stable")
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def parse_tables(source: str) -> tuple[str, dict[str, list[tuple[int, int]]]]:
    version_match = UNICODE_VERSION_PATTERN.search(source)
    if version_match is None:
        raise ValueError("Godot source does not declare its Unicode DerivedCoreProperties version")

    tables: dict[str, list[tuple[int, int]]] = {}
    for match in ARRAY_PATTERN.finditer(source):
        name = match.group("name")
        ranges = [
            (int(first, 16), int(last, 16))
            for first, last in RANGE_PATTERN.findall(match.group("body"))
        ]
        expected = int(match.group("count"))
        if len(ranges) != expected:
            raise ValueError(f"{name} contains {len(ranges)} ranges, expected {expected}")
        if any(first > last for first, last in ranges):
            raise ValueError(f"{name} contains an inverted range")
        if any(previous[1] >= current[0] for previous, current in zip(ranges, ranges[1:])):
            raise ValueError(f"{name} ranges are overlapping or unsorted")
        tables[name] = ranges

    if set(tables) != {"xid_start", "xid_continue"}:
        raise ValueError("Godot source is missing XID_Start or XID_Continue")
    return version_match.group(1), tables


def render_table(name: str, ranges: list[tuple[int, int]]) -> list[str]:
    lines = [f"inline constexpr std::array<UnicodeRange, {len(ranges)}> {name}_ranges{{{{"]
    for first, last in ranges:
        lines.append(f"    UnicodeRange{{0x{first:x}U, 0x{last:x}U}},")
    lines.append("}};")
    return lines


def parse_confusables(source: str) -> dict[int, tuple[int, ...]]:
    mappings: dict[int, tuple[int, ...]] = {}
    for line_number, raw_line in enumerate(source.splitlines(), 1):
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        fields = [field.strip() for field in line.split(";")]
        if len(fields) < 2:
            raise ValueError(f"invalid confusables line {line_number}")
        source_values = tuple(int(value, 16) for value in fields[0].split())
        if len(source_values) != 1:
            raise ValueError(f"confusable source is not one codepoint at line {line_number}")
        target = tuple(int(value, 16) for value in fields[1].split())
        if not target:
            raise ValueError(f"empty confusable target at line {line_number}")
        codepoint = source_values[0]
        if codepoint in mappings and mappings[codepoint] != target:
            raise ValueError(f"conflicting confusable mapping for U+{codepoint:04X}")
        mappings[codepoint] = target
    return mappings


def parse_unicode_data(
    source: str,
) -> tuple[dict[int, tuple[int, ...]], dict[int, int]]:
    decompositions: dict[int, tuple[int, ...]] = {}
    combining_classes: dict[int, int] = {}
    for line_number, line in enumerate(source.splitlines(), 1):
        if not line:
            continue
        fields = line.split(";")
        if len(fields) != 15:
            raise ValueError(f"invalid UnicodeData line {line_number}")
        codepoint = int(fields[0], 16)
        combining_class = int(fields[3])
        if combining_class:
            if combining_class > 255:
                raise ValueError(f"invalid combining class at line {line_number}")
            combining_classes[codepoint] = combining_class
        decomposition = fields[5].strip()
        if decomposition and not decomposition.startswith("<"):
            decompositions[codepoint] = tuple(int(value, 16) for value in decomposition.split())
    return decompositions, combining_classes


def parse_default_ignorables(source: str) -> list[tuple[int, int]]:
    ranges: list[tuple[int, int]] = []
    for raw_line in source.splitlines():
        match = DEFAULT_IGNORABLE_PATTERN.match(raw_line)
        if match is None:
            continue
        first = int(match.group(1), 16)
        last = int(match.group(2), 16) if match.group(2) else first
        if ranges and first <= ranges[-1][1] + 1:
            ranges[-1] = (ranges[-1][0], max(ranges[-1][1], last))
        else:
            ranges.append((first, last))
    if not ranges:
        raise ValueError("DerivedCoreProperties has no Default_Ignorable_Code_Point ranges")
    return ranges


def render_mapping_table(name: str, mappings: dict[int, tuple[int, ...]]) -> list[str]:
    values: list[int] = []
    records: list[tuple[int, int, int]] = []
    for codepoint, mapping in sorted(mappings.items()):
        records.append((codepoint, len(values), len(mapping)))
        values.extend(mapping)
    lines = [f"inline constexpr std::array<std::uint32_t, {len(values)}> {name}_values{{{{"]
    for index in range(0, len(values), 8):
        rendered = ", ".join(f"0x{value:x}U" for value in values[index : index + 8])
        lines.append(f"    {rendered},")
    lines.append("}};")
    lines.append(
        f"inline constexpr std::array<UnicodeMapping, {len(records)}> {name}_mappings{{{{"
    )
    for codepoint, offset, length in records:
        lines.append(
            f"    UnicodeMapping{{0x{codepoint:x}U, {offset}U, {length}U}},"
        )
    lines.append("}};")
    return lines


def render_combining_classes(values: dict[int, int]) -> list[str]:
    lines = [
        "inline constexpr std::array<UnicodeCombiningClass, "
        f"{len(values)}> canonical_combining_classes{{{{"
    ]
    for codepoint, combining_class in sorted(values.items()):
        lines.append(
            f"    UnicodeCombiningClass{{0x{codepoint:x}U, {combining_class}U}},"
        )
    lines.append("}};")
    return lines


def main() -> int:
    arguments = parse_arguments()
    source_bytes = arguments.godot_char_range.read_bytes()
    source = source_bytes.decode("utf-8")
    unicode_version, tables = parse_tables(source)
    digest = hashlib.sha256(source_bytes).hexdigest()
    confusable_bytes = arguments.confusables.read_bytes()
    unicode_data_bytes = arguments.unicode_data.read_bytes()
    derived_bytes = arguments.derived_core_properties.read_bytes()
    confusables = parse_confusables(confusable_bytes.decode("utf-8"))
    decompositions, combining_classes = parse_unicode_data(unicode_data_bytes.decode("utf-8"))
    default_ignorables = parse_default_ignorables(derived_bytes.decode("utf-8"))

    output = [
        "// Generated file. Do not edit manually.",
        f"// Godot source tag: {arguments.godot_tag}",
        f"// Unicode DerivedCoreProperties: {unicode_version}",
        f"// Godot char_range.cpp SHA-256: {digest}",
        f"// Unicode confusables.txt SHA-256: {hashlib.sha256(confusable_bytes).hexdigest()}",
        f"// Unicode UnicodeData.txt SHA-256: {hashlib.sha256(unicode_data_bytes).hexdigest()}",
        "// Unicode DerivedCoreProperties.txt SHA-256: "
        f"{hashlib.sha256(derived_bytes).hexdigest()}",
        "// Source behavior and range layout are provided under the Godot Engine MIT License.",
        "// Unicode data is used under the Unicode License v3.",
        "",
    ]
    output.extend(render_table("xid_start", tables["xid_start"]))
    output.append("")
    output.extend(render_table("xid_continue", tables["xid_continue"]))
    output.append("")
    output.extend(render_mapping_table("canonical_decomposition", decompositions))
    output.append("")
    output.extend(render_combining_classes(combining_classes))
    output.append("")
    output.extend(render_table("default_ignorable", default_ignorables))
    output.append("")
    output.extend(render_mapping_table("confusable", confusables))
    output.append("")

    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text("\n".join(output), encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
