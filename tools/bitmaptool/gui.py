"""Tk GUI: asset cards, pixel editor and OLED simulator."""

import tkinter as tk
from tkinter import filedialog, messagebox, LabelFrame
from tkinter import scrolledtext
from pathlib import Path
from typing import Optional
import random
import subprocess
import time


from .canvas import OLEDCanvas
from .constants import *  # noqa: F403
from .convert import Converter, render_header, upsert_entry, write_atomic
from .rain import RainAnimation
from .scene import SceneState, SceneComposer, VIEW_CLOCK, VIEW_HOURLY, VIEW_TODAY, VIEW_WEEKLY
from .slots import *  # noqa: F403
from . import theme

BG_COLOR = "#1a1a1a"
FG_COLOR = "#ffffff"
ENTRY_BG = "#2b2b2b"


# ──────────────────────────────────────────────────────────────────────────────
# BitmapEditorDialog — Simple click-to-toggle pixel editor
# ──────────────────────────────────────────────────────────────────────────────

class BitmapEditorDialog(tk.Toplevel):
    """Modal dialog for editing a bitmap with click-to-toggle pixels."""

    def __init__(self, parent, name: str, width: int, height: int,
                 existing_bitmap: Optional[list[list[bool]]] = None):
        super().__init__(parent)
        self.title(f"Edit: {name}")
        self.transient(parent)
        self.grab_set()

        self.name = name
        self.width = width
        self.height = height
        self.zoom = 10  # 10x zoom for each pixel
        self.result_bitmap: Optional[list[list[bool]]] = None
        self._max_history = 20
        self._history: list[list[list[bool]]] = []
        self._history_index = -1

        # Initialize or copy bitmap
        if existing_bitmap:
            self.bitmap = [row[:] for row in existing_bitmap]
        else:
            self.bitmap = [[False] * width for _ in range(height)]

        self._save_history()
        self._build_ui()
        self._render_grid()

    def _build_ui(self):
        frame = tk.Frame(self, padx=10, pady=10)
        frame.pack(fill=tk.BOTH, expand=True)

        # Canvas for zoomed pixel grid
        self.canvas = tk.Canvas(
            frame,
            width=self.width * self.zoom,
            height=self.height * self.zoom,
            bg="#222222",
            highlightthickness=1,
            highlightbackground="#444444",
        )
        self.canvas.pack()

        # Bind click to toggle pixel
        self.canvas.bind("<Button-1>", self._on_canvas_click)

        # Button row
        btn_frame = tk.Frame(frame)
        btn_frame.pack(fill=tk.X, pady=(10, 0))
        tk.Button(btn_frame, text="Undo (Ctrl+Z)", command=self._undo).pack(side=tk.LEFT, padx=4)
        tk.Button(btn_frame, text="Redo (Ctrl+Y)", command=self._redo).pack(side=tk.LEFT, padx=4)
        tk.Button(btn_frame, text="Clear All", command=self._on_clear).pack(side=tk.LEFT, padx=4)
        tk.Button(btn_frame, text="Fill All", command=self._on_fill).pack(side=tk.LEFT, padx=4)
        tk.Button(btn_frame, text="Cancel", command=self.destroy).pack(side=tk.RIGHT, padx=4)
        tk.Button(btn_frame, text="Save", command=self._on_save,
                  bg="#44aa44", fg="white").pack(side=tk.RIGHT, padx=4)

        # Keyboard shortcuts for undo/redo
        self.bind("<Control-z>", lambda e: self._undo())
        self.bind("<Control-Z>", lambda e: self._undo())
        self.bind("<Control-y>", lambda e: self._redo())
        self.bind("<Control-Y>", lambda e: self._redo())

    def _render_grid(self):
        """Draw the zoomed pixel grid."""
        self.canvas.delete("all")
        for y in range(self.height):
            for x in range(self.width):
                on = self.bitmap[y][x]
                color = theme.on if on else theme.off
                self.canvas.create_rectangle(
                    x * self.zoom, y * self.zoom,
                    (x + 1) * self.zoom, (y + 1) * self.zoom,
                    fill=color, outline="#444444", width=1,
                )

    def _on_canvas_click(self, event):
        """Toggle pixel at click position."""
        x = event.x // self.zoom
        y = event.y // self.zoom
        if 0 <= x < self.width and 0 <= y < self.height:
            self.bitmap[y][x] = not self.bitmap[y][x]
            self._save_history()
            self._render_grid()

    def _on_clear(self):
        self.bitmap = [[False] * self.width for _ in range(self.height)]
        self._save_history()
        self._render_grid()

    def _on_fill(self):
        self.bitmap = [[True] * self.width for _ in range(self.height)]
        self._save_history()
        self._render_grid()

    def _save_history(self):
        """Save current bitmap state to history stack."""
        # Remove any states after current index (for redo)
        self._history = self._history[:self._history_index + 1]
        # Add current state
        self._history.append([row[:] for row in self.bitmap])
        # Limit history size
        if len(self._history) > self._max_history:
            self._history.pop(0)
        else:
            self._history_index += 1

    def _undo(self):
        """Undo last change."""
        if self._history_index > 0:
            self._history_index -= 1
            self.bitmap = [row[:] for row in self._history[self._history_index]]
            self._render_grid()

    def _redo(self):
        """Redo last undone change."""
        if self._history_index < len(self._history) - 1:
            self._history_index += 1
            self.bitmap = [row[:] for row in self._history[self._history_index]]
            self._render_grid()

    def _on_save(self):
        self.result_bitmap = self.bitmap
        self.destroy()


# ──────────────────────────────────────────────────────────────────────────────
# App — Main tkinter GUI
# ──────────────────────────────────────────────────────────────────────────────

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Dayspeck Bitmap Tool")
        self.geometry("1100x660")
        self.resizable(False, False)
        self.configure(bg=BG_COLOR)

        self.canvas = OLEDCanvas()
        self.state = SceneState()
        self.assets: dict[str, list[list[bool]]] = {}
        self.firmware_dir: Optional[Path] = None
        # Tracks currently selected asset slot name
        self._active_slot: Optional[str] = None
        # Holds refs to card widgets keyed by slot name
        self._cards: dict[str, dict] = {}
        # Stores X/Y position for each asset
        self._asset_positions: dict[str, dict] = {}

        # Keyboard shortcuts
        self.bind("<Control-o>", lambda e: self._on_open_firmware_dir())
        self.bind("<Control-O>", lambda e: self._on_open_firmware_dir())
        self.bind("<Control-s>", lambda e: self._on_save_h())
        self.bind("<Control-S>", lambda e: self._on_save_h())
        self.bind("<Control-e>", lambda e: self._on_edit_active_slot())
        self.bind("<Control-E>", lambda e: self._on_edit_active_slot())
        self.bind("<Control-c>", lambda e: self._on_copy())
        self.bind("<Control-C>", lambda e: self._on_copy())

        self._build_left_panel()
        self._build_centre_panel()
        self._build_right_panel()
        self._refresh_preview()
        # Start 15 FPS rain animation loop after GUI is ready
        self._start_rain_animation_loop()

    # ── Left panel: Asset browser ─────────────────────────────────────────

    def _build_left_panel(self):
        outer = LabelFrame(self, text="Asset Browser", padx=8, pady=8)
        outer.pack(side=tk.LEFT, fill=tk.Y, padx=8, pady=8)

        tk.Button(outer, text="Open firmware dir",
                  command=self._on_open_firmware_dir).pack(fill=tk.X, pady=(0, 6))

        # Label showing active bitmaps.h path
        self.firmware_path_lbl = tk.Label(outer, text="No firmware dir loaded",
                                           bg="#2b2b2b", fg="#666666",
                                           font=("TkDefaultFont", 8), anchor="w", wraplength=200)
        self.firmware_path_lbl.pack(fill=tk.X, pady=(0, 8))

        # PNG import options: brightness threshold and polarity (dark artwork on a light background)
        self.threshold_var = tk.IntVar(value=128)
        self.invert_var = tk.BooleanVar(value=False)
        import_opts = tk.Frame(outer)
        import_opts.pack(fill=tk.X, pady=(0, 8))
        tk.Label(import_opts, text="PNG threshold:").pack(side=tk.LEFT)
        tk.Spinbox(import_opts, from_=1, to=254, width=4, textvariable=self.threshold_var).pack(side=tk.LEFT, padx=4)
        tk.Checkbutton(import_opts, text="Invert", variable=self.invert_var).pack(side=tk.LEFT)

        # Scrollable card list
        list_frame = tk.Frame(outer, bd=1, relief=tk.SUNKEN)
        list_frame.pack(fill=tk.BOTH, expand=True)

        # Procedural Graphics section
        proc_frame = LabelFrame(outer, text="Procedural Graphics", padx=8, pady=4)
        proc_frame.pack(fill=tk.X, pady=(8, 0))

        # Map procedural functions to their C++ file and function names
        self._procedural_functions = {
            "Giant Badge": ("firmware/src/display.cpp", "drawGiantBadge"),
            "Rain Effect": ("firmware/src/display.cpp", "drawProceduralRain"),
            "Snow Effect": ("firmware/src/display.cpp", "drawProceduralSnow"),
        }

        for name, (filepath, funcname) in self._procedural_functions.items():
            btn = tk.Button(proc_frame, text=name, font=("TkDefaultFont", 8),
                           command=lambda n=name: self._on_edit_procedural(n))
            btn.pack(fill=tk.X, pady=1)

        self._scroll_canvas = tk.Canvas(list_frame, width=220, bg="#2b2b2b",
                                        highlightthickness=0)
        scrollbar = tk.Scrollbar(list_frame, orient=tk.VERTICAL,
                                 command=self._scroll_canvas.yview)
        self._scroll_canvas.configure(yscrollcommand=scrollbar.set)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        self._scroll_canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        self._cards_frame = tk.Frame(self._scroll_canvas, bg="#2b2b2b")
        self._cards_frame_id = self._scroll_canvas.create_window(
            (0, 0), window=self._cards_frame, anchor="nw"
        )
        self._cards_frame.bind("<Configure>", self._on_cards_frame_configure)

        # Build one card per known asset slot
        for slot in ASSET_SLOTS:
            slot_name, slot_size, slot_desc, slot_x, slot_y = slot
            self._build_asset_card(slot_name, slot_size, slot_desc, slot_x, slot_y)

        # Initialize asset positions dict
        self._asset_positions = {}

    def _on_cards_frame_configure(self, _event=None):
        self._scroll_canvas.configure(
            scrollregion=self._scroll_canvas.bbox("all")
        )

    def _on_edit_procedural(self, name: str):
        """Open VS Code to the C++ function for the selected procedural graphic."""
        if name not in self._procedural_functions:
            return
        filepath, funcname = self._procedural_functions[name]
        # Try to find the file relative to current directory
        # Check if we're in the Dayspeck directory
        base_dir = Path("..") if Path("../firmware").exists() else Path(".")
        full_path = base_dir / filepath
        if not full_path.exists():
            # Try absolute path from current working directory
            full_path = Path.cwd() / filepath
        if not full_path.exists():
            messagebox.showerror("File not found", f"Could not find {filepath}")
            return
        # Open in VS Code at the function
        try:
            # Use code.cmd on Windows - open file without --goto (function name not valid for --goto)
            cmd = ["code.cmd", str(full_path)]
            subprocess.Popen(cmd)
        except FileNotFoundError:
            # Try without code.cmd
            try:
                subprocess.Popen(["code", str(full_path)])
            except FileNotFoundError:
                messagebox.showerror("VS Code not found", "Could not find VS Code (code or code.cmd in PATH)")

    def _get_slot_dims(self, name: str) -> tuple[int, int]:
        """Return (width, height) for a slot name from ASSET_SLOTS."""
        for slot in ASSET_SLOTS:
            slot_name = slot[0]
            slot_size = slot[1]
            if slot_name == name:
                # Parse "64x20" or "10x10" format
                parts = slot_size.lower().replace("×", "x").split("x")
                if len(parts) == 2:
                    return int(parts[0]), int(parts[1])
        return 8, 8  # fallback

    def _build_asset_card(self, name: str, size: str, desc: str, def_x: int = 0, def_y: int = 0):
        """Build a structured card widget for a known asset slot."""
        # Store default position
        if not hasattr(self, '_asset_positions'):
            self._asset_positions = {}
        self._asset_positions[name] = {"x": def_x, "y": def_y}

        card = tk.Frame(self._cards_frame, bg="#3c3c3c", bd=0,
                        padx=6, pady=4, cursor="hand2")
        card.pack(fill=tk.X, padx=4, pady=(4, 0))

        # Bold asset name
        name_lbl = tk.Label(card, text=name, bg="#3c3c3c", fg="#ffffff",
                             font=("TkDefaultFont", 9, "bold"), anchor="w")
        name_lbl.pack(fill=tk.X)

        # Size / description
        tk.Label(card, text=f"{size}  ·  {desc}", bg="#3c3c3c", fg="#aaaaaa",
                 font=("TkDefaultFont", 8), anchor="w").pack(fill=tk.X)

        # Position row
        pos_frame = tk.Frame(card, bg="#3c3c3c")
        pos_frame.pack(fill=tk.X, pady=(2, 0))
        tk.Label(pos_frame, text="X:", bg="#3c3c3c", fg="#888888",
                 font=("TkDefaultFont", 8)).pack(side=tk.LEFT)
        spin_x = tk.Spinbox(pos_frame, from_=0, to=127, width=4, font=("TkDefaultFont", 8))
        spin_x.delete(0, "end")
        spin_x.insert(0, str(def_x))
        spin_x.pack(side=tk.LEFT, padx=(0, 4))
        tk.Label(pos_frame, text="Y:", bg="#3c3c3c", fg="#888888",
                 font=("TkDefaultFont", 8)).pack(side=tk.LEFT)
        spin_y = tk.Spinbox(pos_frame, from_=0, to=63, width=4, font=("TkDefaultFont", 8))
        spin_y.delete(0, "end")
        spin_y.insert(0, str(def_y))
        spin_y.pack(side=tk.LEFT, padx=(0, 4))

        # Status line (updated when firmware dir is loaded)
        status_lbl = tk.Label(card, text="Not loaded", bg="#3c3c3c", fg="#666666",
                               font=("TkDefaultFont", 8, "italic"), anchor="w")
        status_lbl.pack(fill=tk.X)

        # Button row: "Select file…" | "Edit"
        btn_frame = tk.Frame(card, bg="#3c3c3c")
        btn_frame.pack(anchor="w", pady=(2, 0))

        btn = tk.Button(btn_frame, text="Select file…", font=("TkDefaultFont", 8),
                        bg="#555555", fg="#eeeeee", activebackground="#777777",
                        bd=0, padx=4, pady=1,
                        command=lambda n=name: self._on_select_file_for_slot(n))
        btn.pack(side=tk.LEFT, padx=(0, 4))

        edit_btn = tk.Button(btn_frame, text="Edit", font=("TkDefaultFont", 8),
                             bg="#555555", fg="#eeeeee", activebackground="#777777",
                             bd=0, padx=4, pady=1,
                             command=lambda n=name: self._on_edit_slot(n))
        edit_btn.pack(side=tk.LEFT)

        # Click anywhere on card to activate slot
        for widget in (card, name_lbl, status_lbl, pos_frame):
            widget.bind("<Button-1>", lambda _e, n=name: self._on_slot_click(n))

        self._cards[name] = {
            "frame": card, "status_lbl": status_lbl, "btn": btn,
            "spin_x": spin_x, "spin_y": spin_y,
                             "edit_btn": edit_btn}

    def _set_card_active(self, name: str):
        """Highlight the selected card, reset others."""
        for slot, refs in self._cards.items():
            active = (slot == name)
            bg = "#4a6fa5" if active else "#3c3c3c"
            refs["frame"].config(bg=bg)
            for child in refs["frame"].winfo_children():
                try:
                    child.configure(bg=bg)
                except Exception:
                    pass

    def _on_edit_slot(self, name: str):
        """Open the bitmap editor dialog for a slot."""
        w, h = self._get_slot_dims(name)
        existing = self.assets.get(name)
        dialog = BitmapEditorDialog(self, name, w, h, existing)
        self.wait_window(dialog)
        if dialog.result_bitmap:
            self.assets[name] = dialog.result_bitmap
            self._update_card_status(name, "Edited in tool", found=True)
            self._on_slot_click(name)

    def _on_edit_active_slot(self):
        """Open the bitmap editor for the currently active slot."""
        if self._active_slot:
            self._on_edit_slot(self._active_slot)
        elif self.assets:
            first_key = next(iter(self.assets))
            self._on_edit_slot(first_key)

    def _update_card_status(self, name: str, status: str, found: bool = False):
        """Update the status label text and colour for a slot card."""
        if name in self._cards:
            color = "#44cc88" if found else "#666666"
            self._cards[name]["status_lbl"].config(text=status, fg=color)

    # ── Centre panel: OLED preview + C array output ──────────────────────

    def _build_centre_panel(self):
        frame = tk.Frame(self)
        frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=8, pady=8)

        tk.Label(frame, text="OLED Preview (4x scale)").pack(anchor=tk.W)
        self.preview_canvas = tk.Canvas(
            frame,
            width=CANVAS_W * DEFAULT_SCALE,
            height=CANVAS_H * DEFAULT_SCALE,
            bg=theme.off,
            highlightthickness=1,
            highlightbackground="#333",
        )
        self.preview_canvas.pack(pady=(0, 8))

        tk.Label(frame, text="C Array Output").pack(anchor=tk.W)
        self.c_array_text = scrolledtext.ScrolledText(frame, width=60, height=12, font=("Courier", 9))
        self.c_array_text.pack(fill=tk.BOTH, expand=True)

        btn_frame = tk.Frame(frame)
        btn_frame.pack(fill=tk.X, pady=(4, 0))
        tk.Button(btn_frame, text="Copy", command=self._on_copy).pack(side=tk.LEFT, padx=4)
        tk.Button(btn_frame, text="Save to .h", command=self._on_save_h).pack(side=tk.LEFT, padx=4)

    # ── Right panel: Controls ────────────────────────────────────────────

    def _build_right_panel(self):
        # Create a canvas with scrollbar for the controls panel
        canvas = tk.Canvas(self, width=260, bg=BG_COLOR, highlightthickness=0)
        scrollbar = tk.Scrollbar(self, orient=tk.VERTICAL, command=canvas.yview, bg=BG_COLOR)
        self._controls_frame = tk.Frame(canvas, bg=BG_COLOR, padx=8, pady=8)
        
        canvas.pack(side=tk.RIGHT, fill=tk.Y, padx=8, pady=8)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        canvas.configure(yscrollcommand=scrollbar.set)
        canvas.create_window((0, 0), window=self._controls_frame, anchor=tk.NW)
        self._controls_frame.bind("<Configure>", lambda _: canvas.configure(scrollregion=canvas.bbox("all")))
        
        # Use the _controls_frame for all controls
        frame = LabelFrame(self._controls_frame, text="Controls", padx=8, pady=8, 
                              bg=BG_COLOR, fg=FG_COLOR)
        frame.pack(fill=tk.X, pady=(0, 8))

        # Badge
        tk.Label(frame, text="Badge:", bg=BG_COLOR, fg=FG_COLOR).pack(anchor=tk.W, pady=(0, 2))
        self.badge_var = tk.StringVar(value="check")
        for val, label in [("check", "✓"), ("warn", "!"), ("x", "X")]:
            tk.Radiobutton(frame, text=label, variable=self.badge_var, value=val,
                           command=self._refresh_preview, bg=BG_COLOR, fg=FG_COLOR,
                           selectcolor=BG_COLOR, activebackground=BG_COLOR, activeforeground=FG_COLOR).pack(anchor=tk.W)

        # Weather
        tk.Label(frame, text="Weather:", bg=BG_COLOR, fg=FG_COLOR).pack(anchor=tk.W, pady=(12, 2))
        self.weather_var = tk.StringVar(value="clear")
        for val in ("clear", "rain", "snow", "wind"):
            tk.Radiobutton(frame, text=val.title(), variable=self.weather_var, value=val,
                           command=self._refresh_preview, bg=BG_COLOR, fg=FG_COLOR,
                           selectcolor=BG_COLOR, activebackground=BG_COLOR, activeforeground=FG_COLOR).pack(anchor=tk.W)

        # Intensity
        tk.Label(frame, text="Intensity (1-3):", bg=BG_COLOR, fg=FG_COLOR).pack(anchor=tk.W, pady=(12, 2))
        self.intensity_scale = tk.Scale(frame, from_=1, to=3, orient=tk.HORIZONTAL,
                                        bg=BG_COLOR, fg=FG_COLOR, highlightthickness=0)
        self.intensity_scale.bind("<ButtonRelease-1>", lambda _: self._refresh_preview())
        self.intensity_scale.pack(fill=tk.X)

        # Wind speed
        tk.Label(frame, text="Wind speed (km/h):", bg=BG_COLOR, fg=FG_COLOR).pack(anchor=tk.W, pady=(12, 2))
        self.wind_scale = tk.Scale(frame, from_=0, to=60, orient=tk.HORIZONTAL,
                                   bg=BG_COLOR, fg=FG_COLOR, highlightthickness=0)
        self.wind_scale.bind("<ButtonRelease-1>", lambda _: self._refresh_preview())
        self.wind_scale.pack(fill=tk.X)

        # Night mode
        self.night_var = tk.BooleanVar(value=False)
        tk.Checkbutton(frame, text="Night mode", variable=self.night_var,
                       command=self._refresh_preview, bg=BG_COLOR, fg=FG_COLOR,
                       selectcolor=BG_COLOR, activebackground=BG_COLOR, activeforeground=FG_COLOR).pack(anchor=tk.W, pady=(12, 2))

        # Autumn: leaves blow along with the wind
        self.autumn_var = tk.BooleanVar(value=False)
        tk.Checkbutton(frame, text="Autumn (leaves in the wind)", variable=self.autumn_var,
                       command=self._refresh_preview, bg=BG_COLOR, fg=FG_COLOR,
                       selectcolor=BG_COLOR, activebackground=BG_COLOR, activeforeground=FG_COLOR).pack(anchor=tk.W, pady=(0, 2))

        # Rain animation controls
        tk.Label(frame, text="Rain animation:", bg=BG_COLOR, fg=FG_COLOR).pack(anchor=tk.W, pady=(12, 2))
        self.sprite_rain_var = tk.BooleanVar(value=True)
        tk.Checkbutton(frame, text="Sprite rain (vs lines)", variable=self.sprite_rain_var,
                       command=self._on_rain_mode_change, bg=BG_COLOR, fg=FG_COLOR,
                       selectcolor=BG_COLOR, activebackground=BG_COLOR, activeforeground=FG_COLOR).pack(anchor=tk.W)
        self.show_horizon_var = tk.BooleanVar(value=True)
        tk.Checkbutton(frame, text="Show horizon line", variable=self.show_horizon_var,
                       command=self._refresh_preview, bg=BG_COLOR, fg=FG_COLOR,
                       selectcolor=BG_COLOR, activebackground=BG_COLOR, activeforeground=FG_COLOR).pack(anchor=tk.W)
        tk.Label(frame, text="Horizon Y position:", bg=BG_COLOR, fg=FG_COLOR).pack(anchor=tk.W, pady=(4, 0))
        self.horizon_y_scale = tk.Scale(frame, from_=0, to=63, orient=tk.HORIZONTAL,
                                        bg=BG_COLOR, fg=FG_COLOR, highlightthickness=0)
        self.horizon_y_scale.set(46)
        self.horizon_y_scale.bind("<ButtonRelease-1>", lambda _: self._refresh_preview())
        self.horizon_y_scale.pack(fill=tk.X)
        
        tk.Label(frame, text="Rain intensity (mm/h):", bg=BG_COLOR, fg=FG_COLOR).pack(anchor=tk.W, pady=(4, 0))
        self.rain_intensity_scale = tk.Scale(frame, from_=0, to=20, orient=tk.HORIZONTAL, resolution=0.5,
                                             bg=BG_COLOR, fg=FG_COLOR, highlightthickness=0)
        self.rain_intensity_scale.set(5.0)
        self.rain_intensity_scale.bind("<ButtonRelease-1>", lambda _: self._refresh_preview())
        self.rain_intensity_scale.pack(fill=tk.X)
        
        tk.Label(frame, text="Animation FPS:", bg=BG_COLOR, fg=FG_COLOR).pack(anchor=tk.W, pady=(4, 0))
        self.rain_fps_scale = tk.Scale(frame, from_=1, to=60, orient=tk.HORIZONTAL,
                                       bg=BG_COLOR, fg=FG_COLOR, highlightthickness=0)
        self.rain_fps_scale.set(15)
        self.rain_fps_scale.bind("<ButtonRelease-1>", lambda _: self._refresh_preview())
        self.rain_fps_scale.pack(fill=tk.X)
        
        tk.Button(frame, text="Reset rain drops", font=("TkDefaultFont", 8),
                  command=self._on_reset_rain, bg=BG_COLOR, fg=FG_COLOR).pack(anchor=tk.W, pady=(4, 0))

        # Color scheme
        tk.Label(frame, text="Color scheme:", bg=BG_COLOR, fg=FG_COLOR).pack(anchor=tk.W, pady=(12, 2))
        self.scheme_var = tk.StringVar(value=DEFAULT_SCHEME)
        scheme_menu = tk.OptionMenu(frame, self.scheme_var,
                                     *COLOR_SCHEMES.keys(),
                                     command=lambda _: self._on_scheme_change())
        scheme_menu.configure(width=12)
        scheme_menu.pack(anchor=tk.W)

        # View mode
        tk.Label(frame, text="View mode:", bg=BG_COLOR, fg=FG_COLOR).pack(anchor=tk.W, pady=(12, 2))
        self.view_var = tk.StringVar(value=VIEW_TODAY)
        for val, label in [(VIEW_TODAY, "Today"), (VIEW_WEEKLY, "Weekly"), (VIEW_HOURLY, "Hours"), (VIEW_CLOCK, "Clock")]:
            tk.Radiobutton(frame, text=label, variable=self.view_var, value=val,
                           command=self._on_view_change, bg=BG_COLOR, fg=FG_COLOR,
                           selectcolor=BG_COLOR, activebackground=BG_COLOR, activeforeground=FG_COLOR).pack(anchor=tk.W)

        # Temperature
        tk.Label(frame, text="Temp overlay:", bg=BG_COLOR, fg=FG_COLOR).pack(anchor=tk.W, pady=(12, 2))
        self.temp_entry = tk.Entry(frame, width=12, bg=BG_COLOR, fg=FG_COLOR, insertbackground=FG_COLOR)
        self.temp_entry.insert(0, "3°C")
        self.temp_entry.pack(fill=tk.X)
        self.temp_entry.bind("<Return>", lambda _: self._refresh_preview())

        # Trend arrow
        tk.Label(frame, text="Trend arrow:", bg=BG_COLOR, fg=FG_COLOR).pack(anchor=tk.W, pady=(12, 2))
        self.trend_var = tk.StringVar(value="flat")
        for val, label in [("up", "↑"), ("flat", "→"), ("down", "↓")]:
            tk.Radiobutton(frame, text=label, variable=self.trend_var, value=val,
                           command=self._refresh_preview, bg=BG_COLOR, fg=FG_COLOR,
                           selectcolor=BG_COLOR, activebackground=BG_COLOR, activeforeground=FG_COLOR).pack(anchor=tk.W)

        # Precipitation
        tk.Label(frame, text="Precip (mm):", bg=BG_COLOR, fg=FG_COLOR).pack(anchor=tk.W, pady=(12, 2))
        self.precip_scale = tk.Scale(frame, from_=0, to=10, orient=tk.HORIZONTAL,
                                    bg=BG_COLOR, fg=FG_COLOR, highlightthickness=0)
        self.precip_scale.bind("<ButtonRelease-1>", lambda _: self._refresh_preview())
        self.precip_scale.set(0)
        self.precip_scale.pack(fill=tk.X)

        # Refresh
        tk.Button(frame, text="Refresh preview", command=self._refresh_preview,
                   bg=BG_COLOR, fg=FG_COLOR).pack(pady=(16, 0))

    # ── Event handlers ───────────────────────────────────────────────────

    # Mapping from slot name to the SceneState attribute it populates
    _SLOT_TO_STATE_ATTR = {
        "scene_day":     "scene_day_bmp",
        "scene_night":   "scene_night_bmp",
        "sun":           "sun_bmp",
        "moon":          "moon_bmp",
        "arrow_ur":      "arrow_ur_bmp",
        "arrow_dr":      "arrow_dr_bmp",
        "arrow_r":       "arrow_r_bmp",
    }

    def _load_asset_into_state(self, name: str):
        """Push an asset bitmap from self.assets into the correct SceneState field."""
        if name not in self.assets:
            return
        bmp = self.assets[name]
        # Try exact match first, then try stripping _bmp suffix
        attr = self._SLOT_TO_STATE_ATTR.get(name)
        if not attr and name.endswith("_bmp"):
            attr = self._SLOT_TO_STATE_ATTR.get(name[:-4])  # strip "_bmp"
        if attr:
            setattr(self.state, attr, bmp)
            return

        # Handle rain drop sprites — collect all variants into the rain_sprites list
        if name in RAIN_SPRITE_SLOTS:
            # Rebuild full rain_sprites list from all loaded variants in order
            sprites = []
            for slot in RAIN_SPRITE_SLOTS:
                if slot in self.assets:
                    sprites.append(self.assets[slot])
            self.state.rain_sprites = sprites
            # Reset animation so new sprites take effect
            self.state.rain_drops = []
            self.state.rain_splashes = []
            return

        # Handle splash sprites — collect all variants into the splash_sprites list
        if name in SPLASH_SPRITE_SLOTS:
            # Rebuild full splash_sprites list from all loaded variants in order
            sprites = []
            for slot in SPLASH_SPRITE_SLOTS:
                if slot in self.assets:
                    sprites.append(self.assets[slot])
            self.state.splash_sprites = sprites
            # Reset animation so new sprites take effect
            self.state.rain_drops = []
            self.state.rain_splashes = []
            return

        # Handle leaf sprites: all poses in order
        if name in LEAF_SPRITE_SLOTS:
            self.state.leaf_bmps = [self.assets[slot] for slot in LEAF_SPRITE_SLOTS if slot in self.assets]
            return

    def _on_slot_click(self, name: str):
        """Activate an asset slot card and load its bitmap into the preview."""
        self._active_slot = name
        self._set_card_active(name)

        # Update position from spinboxes if card exists
        if name in self._cards:
            card = self._cards[name]
            if "spin_x" in card and "spin_y" in card:
                try:
                    x = int(card["spin_x"].get())
                    y = int(card["spin_y"].get())
                    self._asset_positions[name] = {"x": x, "y": y}
                except ValueError:
                    pass  # Keep default if invalid

        if name in self.assets:
            self._load_asset_into_state(name)
            self._refresh_preview()
            c_code = Converter.to_c_array(self.assets[name], name)
            self.c_array_text.delete("1.0", tk.END)
            self.c_array_text.insert(tk.END, c_code)

    def _on_select_file_for_slot(self, name: str):
        """Open file dialog to import a PNG into a specific named slot."""
        path = filedialog.askopenfilename(
            title=f"Select PNG for '{name}'",
            filetypes=[("PNG files", "*.png"), ("All files", "*.*")],
        )
        if not path:
            return
        try:
            bitmap = Converter.load_png(path, threshold=self.threshold_var.get(), invert=self.invert_var.get())
            expected = self._get_slot_dims(name)
            actual = (len(bitmap[0]) if bitmap else 0, len(bitmap))
            if expected and expected != actual:
                if not messagebox.askyesno(
                    "Size mismatch",
                    f"'{name}' expects {expected[0]}x{expected[1]} pixels but the image is "
                    f"{actual[0]}x{actual[1]}.\n\nImport it anyway?",
                ):
                    return
            self.assets[name] = bitmap
            self._update_card_status(name, f"Loaded from file", found=True)
            # Auto-activate this slot
            self._on_slot_click(name)
        except Exception as e:
            messagebox.showerror("Import Error", str(e))

    def _on_open_firmware_dir(self):
        dir_path = filedialog.askdirectory(title="Select firmware/include directory")
        if not dir_path:
            return
        self.firmware_dir = Path(dir_path)
        # Update the label showing the active path
        self.firmware_path_lbl.config(text=f"Dir: {self.firmware_dir.name}", fg="#44cc88")
        bitmaps_h = self.firmware_dir / "bitmaps.h"
        if bitmaps_h.exists():
            try:
                parsed = Converter.parse_bitmaps_h(str(bitmaps_h))
                count = 0
                for name, bitmap in parsed.items():
                    self.assets[name] = bitmap
                    # Map full name (sun_bmp) to short name (sun) for card lookup
                    short_name = name[:-4] if name.endswith("_bmp") else name
                    self._update_card_status(short_name, "Found in bitmaps.h", found=True)
                    # Also load into SceneState so preview updates immediately
                    self._load_asset_into_state(name)
                    count += 1
                if count == 0:
                    messagebox.showinfo("Info", "bitmaps.h found but contains no parseable arrays.")
                else:
                    self._refresh_preview()
            except Exception as e:
                messagebox.showerror("Parse Error", str(e))
        else:
            messagebox.showinfo("Info", "No bitmaps.h found in selected directory.")

    def _on_scheme_change(self):
        """Handle color scheme dropdown change."""
        scheme = self.scheme_var.get()
        theme.on, theme.off = COLOR_SCHEMES[scheme]
        self._refresh_preview()

    def _on_view_change(self):
        """Handle view mode radio button change."""
        self.state.view_mode = self.view_var.get()
        self._refresh_preview()

    def _update_state_from_positions(self):
        """Build extra_bmps list from asset positions stored in _asset_positions."""
        # Clear and rebuild extra_bmps
        self.state.extra_bmps = []
        
        # Assets that go into extra_bmps with custom positions
        position_assets = ["sun", "moon", "arrow_ur", "arrow_dr", "arrow_r"]

        for name in position_assets:
            if name in self.assets and name in self._asset_positions:
                pos = self._asset_positions[name]
                x = pos.get("x", 0)
                y = pos.get("y", 0)
                self.state.extra_bmps.append((x, y, self.assets[name]))

    def _render_canvas(self):
        """Fast canvas rendering using to_photoimage - used by animation loop."""
        SceneComposer.compose(self.canvas, self.state)
        
        self._tk_photo = self.canvas.to_photoimage(scale=DEFAULT_SCALE)
        
        if not hasattr(self, '_preview_img_id'):
            self.preview_canvas.delete("all")
            self._preview_img_id = self.preview_canvas.create_image(0, 0, anchor=tk.NW, image=self._tk_photo)
        else:
            self.preview_canvas.itemconfig(self._preview_img_id, image=self._tk_photo)

    def _refresh_preview(self):
        # Update positions from spinboxes for all loaded assets
        for name, card in self._cards.items():
            if "spin_x" in card and "spin_y" in card:
                try:
                    x = int(card["spin_x"].get())
                    y = int(card["spin_y"].get())
                    self._asset_positions[name] = {"x": x, "y": y}
                except (ValueError, KeyError):
                    pass

        # Build extra_bmps from current positions
        self._update_state_from_positions()

        self.state.badge_type = self.badge_var.get()
        self.state.weather = self.weather_var.get()
        self.state.intensity = int(self.intensity_scale.get())
        self.state.wind_speed = int(self.wind_scale.get())
        self.state.night = self.night_var.get()
        self.state.autumn = self.autumn_var.get()
        self.state.temp_str = self.temp_entry.get()
        self.state.trend = self.trend_var.get()
        self.state.precip_mm = float(self.precip_scale.get())
        self.state.seed = 42
        self.state.use_sprite_rain = self.sprite_rain_var.get()
        self.state.show_horizon = self.show_horizon_var.get()
        self.state.horizon_y = int(self.horizon_y_scale.get())
        self.state.rain_intensity_mmh = float(self.rain_intensity_scale.get())
        self.state.rain_fps = int(self.rain_fps_scale.get())

        # Use fast canvas rendering
        self._render_canvas()

        # Update C array output using the active slot name
        if self._active_slot in self.assets:
            c_code = Converter.to_c_array(
                self.assets[self._active_slot],
                self._active_slot,
            )
            self.c_array_text.delete("1.0", tk.END)
            self.c_array_text.insert(tk.END, c_code)

    def _on_rain_mode_change(self):
        """Reset rain drops when switching rain mode (sprite vs procedural)."""
        self.state.rain_drops = []
        self.state.rain_splashes = []
        self.state._rain_rng = None
        self._refresh_preview()

    def _on_reset_rain(self):
        """Clear rain animation state so it reinitialises on next render."""
        self.state.rain_drops = []
        self.state.rain_splashes = []
        self.state._rain_rng = None
        self._refresh_preview()

    def _start_rain_animation_loop(self):
        """Start the animation loop with fixed-timestep physics (15 Hz) and variable-rate rendering."""
        self._animation_running = True
        self._last_time = time.time()
        self._accumulator = 0.0
        self._tick_rain()

    def _tick_rain(self):
        """Advance rain physics at a fixed 15 Hz, render at the user-selected FPS."""
        if not getattr(self, '_animation_running', False):
            return

        current_time = time.time()
        delta_time = current_time - self._last_time
        self._last_time = current_time
        self._accumulator += delta_time

        FIXED_TIMESTEP = 1.0 / 15.0  # 66.666... ms per physics tick

        if self.state.weather == "rain" and self.state.use_sprite_rain:
            while self._accumulator >= FIXED_TIMESTEP:
                rng = self.state._rain_rng or random.Random(self.state.seed)
                self.state._rain_rng = rng
                num_splash_variants = len(self.state.splash_sprites) if self.state.splash_sprites else 1
                RainAnimation.update(
                    self.state.rain_drops,
                    self.state.rain_splashes,
                    rng,
                    self.state.horizon_y,
                    num_splash_variants,
                    self.state.wind_speed,
                    self.state.rain_intensity_mmh,
                )
                self._accumulator -= FIXED_TIMESTEP

        self._render_canvas()

        fps = max(1, self.state.rain_fps)
        interval_ms = int(1000 / fps)
        self.after(interval_ms, self._tick_rain)

    def _on_copy(self):
        self.clipboard_clear()
        self.clipboard_append(self.c_array_text.get("1.0", tk.END))

    def _on_save_h(self):
        # Use firmware_dir as initial directory if set
        initial_dir = str(self.firmware_dir) if hasattr(self, 'firmware_dir') and self.firmware_dir else None
        initial_file = "bitmaps.h" if initial_dir else None
        path = filedialog.asksaveasfilename(
            title="Save bitmap to .h file",
            defaultextension=".h",
            filetypes=[("C header", "*.h"), ("All files", "*.*")],
            initialdir=initial_dir,
            initialfile=initial_file,
        )
        if not path:
            return

        new_c_array = self.c_array_text.get("1.0", tk.END).strip()
        if not new_c_array:
            messagebox.showwarning("Nothing to save", "No bitmap data in the output area.")
            return

        new_entries = Converter.parse_entries(new_c_array)
        if not new_entries:
            messagebox.showerror("Invalid format", "Could not parse a bitmap from the C array.")
            return
        new = new_entries[0]

        existing = []
        if Path(path).exists():
            try:
                problems = []
                existing = Converter.parse_entries(Path(path).read_text(encoding="utf-8"), problems)
            except Exception as e:
                messagebox.showerror("Read error", f"Could not read existing file: {e}")
                return
            if problems:
                if not messagebox.askyesno(
                    "Unreadable arrays",
                    "These arrays in the file could not be read and would be DROPPED when the file is "
                    "rewritten:\n\n" + "\n".join(problems) + "\n\nContinue?",
                ):
                    return

        if any(e.name == new.name for e in existing):
            if not messagebox.askyesno("Overwrite?", f"Bitmap '{new.name}' already exists in the file.\n\nOverwrite?"):
                return

        entries, _ = upsert_entry(existing, new)
        try:
            # The whole file is regenerated in canonical form; the previous version is kept as .bak
            write_atomic(path, render_header(entries))
            messagebox.showinfo("Saved", f"Bitmap '{new.name}' saved to {path}\n(previous version: {Path(path).name}.bak)")
        except Exception as e:
            messagebox.showerror("Write error", f"Could not write file: {e}")


def main():
    app = App()
    app.mainloop()
