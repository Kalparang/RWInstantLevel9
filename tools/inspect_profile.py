"""Read-only inspection of Ravenswatch permanent hero progression.

The XP prefix layout is reverse engineered, not an official save specification.
See https://github.com/just-maik/rwedit/blob/main/src/heroes.ts and savefile.ts.
Unlike the original fixed-index scan, this resolves the hero class by its registry
name. Registry metadata is deliberately not described as an instance count.

This validates the header, checksum, registry and hero record prefixes. It does
not decode the remaining nested save data or prove that rewards were unlocked.
Displayed levels are inferred from the stored float and need in-game validation.
The program never writes a save, backup, or output file; JSON goes to stdout.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import zlib


OPEN_MARKER = bytes.fromhex("1111bbaa")
HERO_CLASS = "oCDtHeroProfileData"
HERO_CLASS_HASH = 0x131E8BD7
PROFILE_DATA_HASH = 0x1326AC57
MAX_STORED_XP = 8.0
KNOWN_HEROES = {
    "06bec3d288244542a27852fec4af77ae": "Merlin",
    "b99512bd11c2294c8d34a71173166be4": "Scarlet",
    "b357bdf7e25df142a9e75d78981c491c": "Juliet",
    "529ae338128a3e4aa5e024eb14722148": "Sun Wukong",
    "271d92e59d6e294189c66044c096a50d": "Beowulf",
    "20fc95f8c0bc284cb897a304d43756b3": "Geppetto",
    "133b454345041b4dbbe4aa2107d7b365": "Carmilla",
    "5fbc7e822bba52409cbdaefe6c71dfb1": "The Snow Queen",
    "34111191a6e3dd4cb7eb708f6107bf4d": "Aladdin",
    "b826bc241a33fc4597ba6805b7b81d12": "Romeo",
    "fb942d8689e691489e12d5be0464a439": "The Pied Piper",
    "81b032ae06c31449b68f6b7dd84d9466": "Melusine",
    # Matched to the GUID in the installed Kintaro.herodef resource, 2026-10-13.
    "be4d1ee590a2874186349eaf0e2b89cb": "Kintaro",
}


class ProfileFormatError(ValueError):
    """The input does not match the supported, validated save prefix format."""


def decode_xp(value: float) -> dict:
    """Interpret level-minus-one plus fractional progress; reject unknown ranges."""
    if not math.isfinite(value) or not 0.0 <= value <= MAX_STORED_XP:
        raise ProfileFormatError(f"Unsupported hero XP value: {value!r}")
    level_floor = math.floor(value)
    return {
        "stored_xp": value,
        "inferred_level": level_floor + 1,
        "inferred_fraction": value - level_floor,
        "is_max": value == MAX_STORED_XP,
    }


def inspect_bytes(blob: bytes, source: str = "<memory>") -> dict:
    """Validate one immutable snapshot and return only progression metadata."""
    if len(blob) < 24:
        raise ProfileFormatError("Truncated profile header")
    header_size, reserved, version, stored_crc = struct.unpack_from("<4I", blob)
    if (header_size, reserved, version) != (16, 0, 1):
        raise ProfileFormatError("Unsupported profile header (expected 16, 0, 1)")
    computed_crc = zlib.crc32(blob[16:])
    if computed_crc != stored_crc:
        raise ProfileFormatError(
            f"CRC32 mismatch: stored {stored_crc:08x}, computed {computed_crc:08x}"
        )
    if blob[16:20] != OPEN_MARKER:
        raise ProfileFormatError("Invalid class registry marker")
    class_count = struct.unpack_from("<I", blob, 20)[0]
    if not 1 <= class_count <= 4096:
        raise ProfileFormatError(f"Invalid class count: {class_count}")
    cursor = 24
    registry = []
    names_seen, hashes_seen = set(), set()
    for index in range(class_count):
        if cursor + 4 > len(blob):
            raise ProfileFormatError(f"Truncated registry entry {index}")
        name_size = struct.unpack_from("<I", blob, cursor)[0]
        cursor += 4
        if not 1 <= name_size <= 1024 or cursor + name_size + 16 > len(blob):
            raise ProfileFormatError(f"Invalid registry name length at entry {index}")
        raw_name = blob[cursor : cursor + name_size]
        if any(char < 32 or char > 126 for char in raw_name):
            raise ProfileFormatError(f"Invalid ASCII class name at entry {index}")
        name = raw_name.decode("ascii")
        cursor += name_size
        class_hash, schema_version, metadata, parent_hash = struct.unpack_from(
            "<4I", blob, cursor
        )
        cursor += 16
        if name in names_seen or class_hash in hashes_seen:
            raise ProfileFormatError(f"Duplicate class registry identity: {name}")
        names_seen.add(name)
        hashes_seen.add(class_hash)
        registry.append({
            "index": index,
            "name": name,
            "hash": f"{class_hash:08x}",
            "version": schema_version,
            "metadata": metadata,
            "parent_hash": f"{parent_hash:08x}",
        })

    hero_class = next((entry for entry in registry if entry["name"] == HERO_CLASS), None)
    if hero_class is None:
        raise ProfileFormatError("Hero profile class is absent from the registry")
    if (
        hero_class["hash"] != f"{HERO_CLASS_HASH:08x}"
        or hero_class["parent_hash"] != f"{PROFILE_DATA_HASH:08x}"
        or hero_class["version"] != 1
        or hero_class["metadata"] != 18
    ):
        raise ProfileFormatError("Unsupported hero class schema; XP offset may have changed")

    pattern = OPEN_MARKER + struct.pack("<I", hero_class["index"])
    position = cursor
    heroes = []
    guids_seen = set()
    while True:
        offset = blob.find(pattern, position)
        if offset < 0:
            break
        position = offset + len(pattern)
        if offset + 32 > len(blob):
            raise ProfileFormatError(f"Truncated hero record at 0x{offset:x}")
        guid = blob[offset + 8 : offset + 24].hex()
        if guid == "0" * 32 or guid in guids_seen:
            raise ProfileFormatError(f"Zero or duplicate hero GUID at 0x{offset:x}")
        guids_seen.add(guid)
        xp = struct.unpack_from("<f", blob, offset + 28)[0]
        heroes.append({
            "guid": guid,
            "name": KNOWN_HEROES.get(guid),
            "marker_offset": offset,
            "xp_offset": offset + 28,
            **decode_xp(xp),
        })
    if not heroes:
        raise ProfileFormatError("No hero records found in the supported data section")
    return {
        "source": source,
        "size": len(blob),
        "sha256": hashlib.sha256(blob).hexdigest(),
        "crc32": f"{stored_crc:08x}",
        "crc_valid": True,
        "data_start": cursor,
        "class_count": class_count,
        "hero_class": hero_class,
        "hero_count": len(heroes),
        "heroes": heroes,
        "limits": [
            "Levels are inferred from a reverse-engineered float32 prefix, not checked in game.",
            "Nested records and unlock/achievement state are not decoded.",
            "Class metadata is not an actual hero count; unknown schemas are rejected.",
        ],
    }


def inspect_profile(path: Path | str) -> dict:
    """Open the save read-only and reject a file that changes during the read."""
    path = Path(path)
    with path.open("rb") as stream:
        import os

        before = os.fstat(stream.fileno())
        blob = stream.read()
        after = os.fstat(stream.fileno())
    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
        raise ProfileFormatError("Profile changed while reading; retry when saving has stopped")
    if len(blob) != after.st_size:
        raise ProfileFormatError("Incomplete profile snapshot")
    return inspect_bytes(blob, str(path.resolve()))


def compare_profiles(before: dict, after: dict) -> dict:
    """Compare by stable GUID, not record position, without attributing causality."""
    previous = {hero["guid"]: hero for hero in before["heroes"]}
    current = {hero["guid"]: hero for hero in after["heroes"]}
    changes = []
    for guid in sorted(previous.keys() | current.keys()):
        old, new = previous.get(guid), current.get(guid)
        if old is not None and new is not None and old["stored_xp"] == new["stored_xp"]:
            continue
        changes.append({
            "guid": guid,
            "name": (new or old)["name"],
            "before_stored_xp": old["stored_xp"] if old else None,
            "after_stored_xp": new["stored_xp"] if new else None,
            "before_inferred_level": old["inferred_level"] if old else None,
            "after_inferred_level": new["inferred_level"] if new else None,
            "status": "added" if old is None else "removed" if new is None else "changed",
        })
    return {
        "before_source": before["source"],
        "after_source": after["source"],
        "before_sha256": before["sha256"],
        "after_sha256": after["sha256"],
        "identical_bytes": before["sha256"] == after["sha256"],
        "progression_changes": changes,
        "unchanged_hero_count": len(previous.keys() & current.keys())
        - sum(change["status"] == "changed" for change in changes),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path, help="Profile .ob snapshot to inspect (required; opened read-only)")
    parser.add_argument("--json", action="store_true", help="Print machine-readable JSON to stdout")
    parser.add_argument("--compare", type=Path, metavar="BEFORE", help="Compare the current profile against a saved BEFORE snapshot")
    args = parser.parse_args(argv)
    try:
        profile = inspect_profile(args.path)
        result = profile
        if args.compare:
            result = {"profile": profile, "comparison": compare_profiles(inspect_profile(args.compare), profile)}
    except (OSError, ProfileFormatError) as error:
        if args.json:
            print(json.dumps({"error": str(error)}, ensure_ascii=False))
        else:
            print(f"Error: {error}", file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0
    print(f"Read-only snapshot: {profile['source']}")
    print(f"CRC32 valid; {profile['size']} bytes; hero class index {profile['hero_class']['index']}")
    print(f"SHA256: {profile['sha256']}")
    for hero in profile["heroes"]:
        name = hero["name"] or f"Unknown ({hero['guid']})"
        print(f"{name:20} stored={hero['stored_xp']:.9g}  inferred level={hero['inferred_level']}"
              f"  progress={hero['inferred_fraction']:.2%}  xp_offset=0x{hero['xp_offset']:x}")
    if args.compare:
        comparison = result["comparison"]
        print(f"Compared with: {comparison['before_source']}")
        print(f"Identical bytes: {comparison['identical_bytes']}; progression changes: {len(comparison['progression_changes'])}")
        for change in comparison["progression_changes"]:
            print(f"  {change['name'] or change['guid']}: {change['before_stored_xp']} -> {change['after_stored_xp']} ({change['status']})")
    print("Limit: inferred levels only; check results, unlocks and persistence in the game.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
