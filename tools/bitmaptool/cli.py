"""Command line interface: convert PNGs, regenerate bitmaps.h, render previews headlessly."""

import argparse
import sys
from pathlib import Path

from .convert import (
    Converter, ConvertError, Entry, check_name, export_assets, regenerate, render_header, upsert_entry, write_atomic,
)


def cmd_convert(args) -> int:
    bitmap = Converter.load_png(args.png, args.threshold, args.invert)
    entry = Entry(check_name(args.name) + "_bmp", bitmap, args.comment or "")
    if args.out is None:
        print(Converter.to_c_array(bitmap, args.name, args.comment or ""))
        return 0
    out = Path(args.out)
    problems = []
    existing = Converter.parse_entries(out.read_text(encoding="utf-8"), problems) if out.exists() else []
    for p in problems:
        print(f"warning: {p}", file=sys.stderr)
    entries, replaced = upsert_entry(existing, entry)
    write_atomic(out, render_header(entries))
    print(f"{'Replaced' if replaced else 'Added'} {entry.name} ({entry.width}x{entry.height}) in {out}")
    return 0


def cmd_regen(args) -> int:
    ok = regenerate(args.manifest, args.out, check_only=args.check)
    if args.check:
        print("bitmaps.h is up to date" if ok else "bitmaps.h is OUT OF DATE: run `regen` and commit the result")
        return 0 if ok else 1
    print("bitmaps.h regenerated")
    return 0


def cmd_gui(args) -> int:
    from .gui import main as gui_main
    gui_main()
    return 0


def cmd_export(args) -> int:
    manifest = export_assets(args.header, args.out_dir)
    print(f"Wrote PNG sources and {manifest}")
    return 0


def cmd_render(args) -> int:
    from .canvas import OLEDCanvas
    from .scene import SceneComposer, SceneState, VIEW_CLOCK, VIEW_HOURLY, VIEW_TODAY, VIEW_WEEKLY

    state = SceneState()
    state.weather = args.weather
    state.night = args.night
    state.badge_type = args.badge
    state.temp_str = args.temp
    state.wind_speed = args.wind
    state.view_mode = {"weekly": VIEW_WEEKLY, "hourly": VIEW_HOURLY, "clock": VIEW_CLOCK}.get(args.view, VIEW_TODAY)
    state.week_best_day = args.best_day
    state.tomorrow = args.tomorrow
    state.stale = args.stale
    firmware_header = Path(args.bitmaps)
    if firmware_header.exists():
        by_name = {e.name: e.pixels for e in Converter.parse_entries(firmware_header.read_text(encoding="utf-8"))}
        state.skyline_bmp = by_name.get("skyline_base_bmp")
        state.sun_bmp = by_name.get("sun_bmp")
        state.moon_bmp = by_name.get("moon_bmp")
        state.arrow_ur_bmp = by_name.get("arrow_ur_bmp")
        state.arrow_dr_bmp = by_name.get("arrow_dr_bmp")
        state.arrow_r_bmp = by_name.get("arrow_r_bmp")
        state.rain_sprites = [by_name[f"rain_drop_{i}_bmp"] for i in range(1, 5) if f"rain_drop_{i}_bmp" in by_name]
        state.splash_sprites = [by_name[f"splash_{i}_bmp"] for i in range(1, 5) if f"splash_{i}_bmp" in by_name]
    canvas = OLEDCanvas()
    state.show_horizon = False
    for _ in range(max(1, args.frames)):
        SceneComposer.compose(canvas, state)    # the rain animation advances one step per frame
    canvas.to_image(scale=args.scale).save(args.out)
    print(f"Wrote {args.out}")
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="bitmaptool", description="Dayspeck bitmap tool")
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("gui", help="start the graphical tool")
    p.set_defaults(func=cmd_gui)

    p = sub.add_parser("convert", help="convert one PNG to a C array (print it, or add it to a bitmaps.h)")
    p.add_argument("png")
    p.add_argument("--name", required=True, help="array name, e.g. sun (becomes sun_bmp)")
    p.add_argument("--out", help="bitmaps.h to update; omit to print the array")
    p.add_argument("--threshold", type=int, default=128)
    p.add_argument("--invert", action="store_true", help="dark artwork on a light background")
    p.add_argument("--comment", help="description kept next to the array")
    p.set_defaults(func=cmd_convert)

    p = sub.add_parser("regen", help="rebuild bitmaps.h from a manifest of PNG files")
    p.add_argument("manifest")
    p.add_argument("--out")
    p.add_argument("--check", action="store_true", help="only verify the header is up to date (for CI)")
    p.set_defaults(func=cmd_regen)

    p = sub.add_parser("export", help="write PNG sources and a manifest from an existing bitmaps.h")
    p.add_argument("header")
    p.add_argument("out_dir")
    p.set_defaults(func=cmd_export)

    p = sub.add_parser("render", help="render the OLED scene to a PNG without a GUI")
    p.add_argument("--out", required=True)
    p.add_argument("--bitmaps", default="firmware/include/bitmaps.h")
    p.add_argument("--view", choices=["today", "weekly", "hourly", "clock"], default="today")
    p.add_argument("--best-day", type=int, default=-1, help="weekly view: column to highlight")
    p.add_argument("--tomorrow", action="store_true", help="show the TMR tag")
    p.add_argument("--stale", action="store_true", help="show the OLD tag")
    p.add_argument("--weather", choices=["clear", "rain", "snow", "wind"], default="clear")
    p.add_argument("--night", action="store_true")
    p.add_argument("--badge", choices=["check", "warn", "x"], default="check")
    p.add_argument("--temp", default="12C")
    p.add_argument("--wind", type=int, default=10)
    p.add_argument("--scale", type=int, default=4)
    p.add_argument("--frames", type=int, default=1, help="animation steps to run before the snapshot")
    p.set_defaults(func=cmd_render)

    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except ConvertError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
