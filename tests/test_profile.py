"""Synthetic format checks; no test writes the user's game or save files."""

import contextlib
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock
import zlib

from tools.inspect_profile import (
    HERO_CLASS_HASH,
    KNOWN_HEROES,
    OPEN_MARKER,
    PROFILE_DATA_HASH,
    ProfileFormatError,
    compare_profiles,
    decode_xp,
    inspect_bytes,
    inspect_profile,
    main,
)


SCARLET = bytes.fromhex("b99512bd11c2294c8d34a71173166be4")
KINTARO = bytes.fromhex("be4d1ee590a2874186349eaf0e2b89cb")


def make_profile(records=((SCARLET, 8.0), (KINTARO, 5.25)), hero_index=8, version=1, metadata=18):
    registry = bytearray()
    for index in range(hero_index + 1):
        name = b"oCDtHeroProfileData" if index == hero_index else f"OtherClass{index}".encode()
        registry += struct.pack("<I", len(name)) + name
        registry += struct.pack("<4I", HERO_CLASS_HASH if index == hero_index else index + 1,
                                version if index == hero_index else 1,
                                metadata if index == hero_index else 0,
                                PROFILE_DATA_HASH if index == hero_index else 0xFFFFFFFF)
    data = bytearray()
    # A false match for the old, hard-coded index. It must never be decoded as XP.
    if hero_index != 7:
        data += OPEN_MARKER + struct.pack("<I", 7) + b"unrelated-record" + struct.pack("<If", 0, -1.0)
    for guid, xp in records:
        data += OPEN_MARKER + struct.pack("<I", hero_index) + guid + struct.pack("<If", 0, xp)
        data += bytes.fromhex("2222bbaa")
    body = OPEN_MARKER + struct.pack("<I", hero_index + 1) + registry + data
    return struct.pack("<4I", 16, 0, 1, zlib.crc32(body)) + body


def resign(blob):
    blob = bytearray(blob)
    struct.pack_into("<I", blob, 12, zlib.crc32(blob[16:]))
    return bytes(blob)


class ProfileTests(unittest.TestCase):
    def test_registry_index_is_dynamic_and_old_skin_marker_is_ignored(self):
        for index in (7, 8, 11):
            with self.subTest(index=index):
                profile = inspect_bytes(make_profile(hero_index=index))
                self.assertEqual(profile["hero_class"]["index"], index)
                self.assertEqual(profile["hero_count"], 2)
                self.assertEqual([hero["name"] for hero in profile["heroes"]], ["Scarlet", "Kintaro"])
                self.assertEqual(profile["heroes"][0]["inferred_level"], 9)
                self.assertEqual(profile["heroes"][1]["inferred_level"], 6)
                self.assertAlmostEqual(profile["heroes"][1]["inferred_fraction"], 0.25)

    def test_crc_corruption_is_rejected(self):
        corrupt = bytearray(make_profile())
        corrupt[-1] ^= 1
        with self.assertRaisesRegex(ProfileFormatError, "CRC32 mismatch"):
            inspect_bytes(corrupt)

    def test_truncated_header_and_registry_are_rejected(self):
        with self.assertRaisesRegex(ProfileFormatError, "Truncated profile header"):
            inspect_bytes(b"short")
        with self.assertRaisesRegex(ProfileFormatError, "registry"):
            inspect_bytes(resign(make_profile()[:28]))

    def test_unknown_header_version_is_rejected(self):
        blob = bytearray(make_profile())
        struct.pack_into("<I", blob, 8, 2)
        with self.assertRaisesRegex(ProfileFormatError, "Unsupported profile header"):
            inspect_bytes(blob)

    def test_changed_hero_schema_is_rejected(self):
        for kwargs in ({"version": 2}, {"metadata": 19}):
            with self.subTest(kwargs=kwargs), self.assertRaisesRegex(ProfileFormatError, "Unsupported hero class schema"):
                inspect_bytes(make_profile(**kwargs))

    def test_corrupt_registry_length_is_rejected_before_scanning(self):
        blob = bytearray(make_profile())
        struct.pack_into("<I", blob, 24, 0xFFFFFFFF)
        with self.assertRaisesRegex(ProfileFormatError, "registry name length"):
            inspect_bytes(resign(blob))

    def test_invalid_hero_values_are_errors_not_silently_dropped(self):
        for value in (-0.01, 8.01, float("inf"), float("nan")):
            with self.subTest(value=value), self.assertRaisesRegex(ProfileFormatError, "Unsupported hero XP"):
                inspect_bytes(make_profile(((SCARLET, value),)))

    def test_duplicate_and_zero_guids_are_rejected(self):
        for records in (((SCARLET, 1.0), (SCARLET, 2.0)), ((bytes(16), 1.0),)):
            with self.subTest(records=records), self.assertRaisesRegex(ProfileFormatError, "Zero or duplicate"):
                inspect_bytes(make_profile(records))

    def test_truncated_record_with_valid_crc_is_rejected(self):
        with self.assertRaisesRegex(ProfileFormatError, "Truncated hero record"):
            inspect_bytes(resign(make_profile()[:-10]))

    def test_no_records_is_not_reported_as_success(self):
        with self.assertRaisesRegex(ProfileFormatError, "No hero records"):
            inspect_bytes(make_profile(()))

    def test_new_hero_is_visible_without_invented_name(self):
        unknown = bytes.fromhex("0123456789abcdef0123456789abcdef")
        hero = inspect_bytes(make_profile(((unknown, 0.5),)))["heroes"][0]
        self.assertIsNone(hero["name"])
        self.assertEqual(hero["guid"], unknown.hex())

    def test_xp_level_boundaries(self):
        self.assertEqual(decode_xp(0.0)["inferred_level"], 1)
        self.assertEqual(decode_xp(7.999)["inferred_level"], 8)
        self.assertEqual(decode_xp(8.0)["inferred_level"], 9)
        self.assertTrue(decode_xp(8.0)["is_max"])

    def test_comparison_matches_guid_after_reordering(self):
        before = inspect_bytes(make_profile(), "before")
        after = inspect_bytes(make_profile(((KINTARO, 8.0), (SCARLET, 8.0))), "after")
        result = compare_profiles(before, after)
        self.assertFalse(result["identical_bytes"])
        self.assertEqual(result["unchanged_hero_count"], 1)
        self.assertEqual(len(result["progression_changes"]), 1)
        self.assertEqual(result["progression_changes"][0]["name"], "Kintaro")
        self.assertEqual(result["progression_changes"][0]["after_inferred_level"], 9)

    def test_comparison_tracks_added_removed_and_identical(self):
        before = inspect_bytes(make_profile(((SCARLET, 1.0),)))
        after = inspect_bytes(make_profile(((KINTARO, 0.0),)))
        result = compare_profiles(before, after)
        self.assertEqual({change["status"] for change in result["progression_changes"]}, {"added", "removed"})
        self.assertEqual(result["unchanged_hero_count"], 0)
        identical = compare_profiles(before, before)
        self.assertTrue(identical["identical_bytes"])
        self.assertEqual(identical["progression_changes"], [])

    def test_file_is_opened_only_readonly_and_json_does_not_change_it(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fixture.ob"
            original = make_profile()
            path.write_bytes(original)
            actual_open = Path.open
            modes = []

            def audited_open(target, mode="r", *args, **kwargs):
                modes.append(mode)
                return actual_open(target, mode, *args, **kwargs)

            output = io.StringIO()
            with mock.patch.object(Path, "open", audited_open), contextlib.redirect_stdout(output):
                self.assertEqual(main([str(path), "--json", "--compare", str(path)]), 0)
            self.assertEqual(modes, ["rb", "rb"])
            self.assertTrue(json.loads(output.getvalue())["comparison"]["identical_bytes"])
            self.assertEqual(path.read_bytes(), original)

    def test_cli_missing_file_returns_json_error(self):
        output = io.StringIO()
        with tempfile.TemporaryDirectory() as directory, contextlib.redirect_stdout(output):
            result = main([str(Path(directory) / "missing.ob"), "--json"])
        self.assertEqual(result, 2)
        self.assertIn("error", json.loads(output.getvalue()))


if __name__ == "__main__":
    unittest.main()
