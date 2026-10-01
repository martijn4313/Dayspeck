"""Tests for PNG <-> C array conversion and bitmaps.h round trips (no GUI needed)."""
from pathlib import Path

import pytest
from PIL import Image

from bitmaptool.convert import (
    ConvertError, Converter, Entry, check_name, export_assets, regenerate, render_header,
    save_png, upsert_entry, write_atomic,
)

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "firmware" / "include" / "bitmaps.h"
MANIFEST = ROOT / "tools" / "assets" / "manifest.json"


def bitmap_from(rows):
    return [[c == "#" for c in row] for row in rows]


def test_bit_order_is_msb_first_and_rows_are_padded():
    bmp = bitmap_from(["#.......#", "........."])   # 9 wide -> 2 bytes per row
    text = Converter.to_c_array(bmp, "t")
    assert "0x80, 0x80, 0x00, 0x00" in text
    assert "#define T_BMP_W  9" in text and "#define T_BMP_H  2" in text


def test_round_trip_for_widths_that_are_not_multiples_of_8():
    for width in (1, 7, 8, 9, 15, 17):
        bmp = [[(x + y) % 3 == 0 for x in range(width)] for y in range(5)]
        entries = Converter.parse_entries(Converter.to_c_array(bmp, "shape"))
        assert len(entries) == 1 and entries[0].pixels == bmp


def test_name_validation():
    assert check_name("sun") == "sun" and check_name("sun_bmp") == "sun"
    for bad in ("", "Sun", "1sun", "a-b", "a b", "x;y"):
        with pytest.raises(ConvertError):
            check_name(bad)


def test_png_alpha_threshold_and_invert(tmp_path):
    img = Image.new("RGBA", (3, 1))
    img.putdata([(255, 255, 255, 255),   # opaque white  -> on
                 (0, 0, 0, 255),         # opaque black  -> off
                 (255, 255, 255, 0)])    # transparent   -> off even though white
    path = tmp_path / "a.png"
    img.save(path)
    assert Converter.load_png(str(path)) == [[True, False, False]]
    assert Converter.load_png(str(path), invert=True) == [[False, True, False]]
    assert Converter.load_png(str(path), threshold=250) == [[True, False, False]]


def test_png_save_and_load_are_lossless(tmp_path):
    bmp = bitmap_from(["#.#", ".#.", "#.#"])
    save_png(bmp, tmp_path / "x.png")
    assert Converter.load_png(str(tmp_path / "x.png")) == bmp


def test_parse_reports_broken_arrays():
    text = (
        "const uint8_t PROGMEM a_bmp[] = {\n 0x01,\n};\n#define A_BMP_W  8\n#define A_BMP_H  2\n"   # 1 byte for 2 rows
        "const uint8_t PROGMEM b_bmp[] = {\n 0x01,\n};\n"                                         # no #defines
    )
    problems = []
    assert Converter.parse_entries(text, problems) == []
    assert len(problems) == 2


def test_upsert_keeps_position_and_description():
    old = [Entry("a_bmp", bitmap_from(["#"]), header="a  1x1 first"), Entry("b_bmp", bitmap_from(["."]))]
    new = Entry("a_bmp", bitmap_from(["."]))
    entries, replaced = upsert_entry(old, new)
    assert replaced and [e.name for e in entries] == ["a_bmp", "b_bmp"]
    assert entries[0].pixels == new.pixels and entries[0].header == "a  1x1 first"
    entries, replaced = upsert_entry(old, Entry("c_bmp", bitmap_from(["#"])))
    assert not replaced and [e.name for e in entries] == ["a_bmp", "b_bmp", "c_bmp"]


def test_write_atomic_keeps_a_backup(tmp_path):
    path = tmp_path / "f.h"
    write_atomic(path, "one")
    write_atomic(path, "two")
    assert path.read_text() == "two" and (tmp_path / "f.h.bak").read_text() == "one"
    assert not (tmp_path / "f.h.tmp").exists()


def test_committed_header_round_trips_byte_for_byte():
    text = HEADER.read_text(encoding="utf-8")
    problems = []
    entries = Converter.parse_entries(text, problems)
    assert problems == [] and entries
    assert render_header(entries) == text


def test_committed_header_matches_its_png_sources():
    """If this fails, bitmaps.h and tools/assets are out of sync: run `python -m bitmaptool regen`."""
    assert regenerate(MANIFEST, check_only=True)


def test_export_then_regen_is_stable(tmp_path):
    manifest = export_assets(HEADER, tmp_path)
    out = tmp_path / "bitmaps.h"
    assert regenerate(manifest, out) and out.read_text() == HEADER.read_text()
