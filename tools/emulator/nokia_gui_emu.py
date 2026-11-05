#!/usr/bin/env python3
"""
nokia_gui_emu.py — Realistic Nokia 2660 Flip Hardware Emulator & Live Display

Emulates the Nokia 2660 Flip (TA-1480 / Unisoc UMS9117):
- Authentic handheld phone emulator size (~400x820)
- Auto-starts emulation immediately on launch with live 240x320 display
- Fully responsive interactive Nokia clamshell keypad & PC keyboard bindings
- Tactile button flash feedback and live LCD keypad status readout
- Hardware indicators: PMIC Flashlight Torch LED & Haptic Vibrator
- Collapsible "Tools >>" drawer for UART console and ARMv7-A register monitor
"""

import sys
import os

# Suppress macOS Tkinter deprecation warning
os.environ["TK_SILENCE_DEPRECATION"] = "1"

# Ensure tools/emulator directory is in sys.path for relative imports
EMU_DIR = os.path.dirname(os.path.abspath(__file__))
if EMU_DIR not in sys.path:
    sys.path.insert(0, EMU_DIR)

import time
import threading
import queue
import struct
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from PIL import Image, ImageTk, ImageDraw

from ums9117_emu import UMS9117Emulator, PAGE_SIZE, align_down, align_up
from unicorn import *
from unicorn.arm_const import *

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
PAYLOAD_DIR = os.path.join(REPO_ROOT, "tools/ram_payload/bin")
DUMP_DIR = os.path.join(REPO_ROOT, "firmware/dumps/ta1480_10.02.17.00")


class NokiaGUIEmulator(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Nokia 2660 Flip (TA-1480)")
        self.geometry("400x820")
        self.minsize(390, 800)
        self.configure(bg="#12161c")

        # Emulation State
        self.emu = None
        self.active_emu = None
        self.emu_thread = None
        self.running = False
        self.current_binary = os.path.join(PAYLOAD_DIR, "baremetal_lcd.bin")
        self.current_profile = "payload"
        self.drawer_open = False

        # Queues for Thread-Safe Communication
        self.key_event_queue = queue.Queue()
        self.uart_queue = queue.Queue()
        self.hardware_events = queue.Queue()
        self.screen_queue = queue.Queue(maxsize=10)
        self.reg_queue = queue.Queue(maxsize=10)
        self.status_queue = queue.Queue()

        # Display Settings: Native 240x320 QVGA
        self.disp_w = 240
        self.disp_h = 320
        self.screen_photo = None
        self.raw_fb = bytearray(self.disp_w * self.disp_h * 2)

        # Hardware Indicators
        self.torch_on = False
        self.vibr_on = False
        self.btn_widgets = {}

        self._build_ui()
        self._load_default_splash()

        # Window teardown protocol
        self.protocol("WM_DELETE_WINDOW", self._on_close)

        # Start queue polling loop
        self._poll_queues()

        # Auto-start emulation immediately on launch!
        self.after(50, self._start_emulation)

    def _on_close(self):
        self.running = False
        self._stop_emulation()
        self.destroy()

    def _build_ui(self):
        # Master container for horizontal layout (Phone on left, Drawer on right)
        self.master_frame = tk.Frame(self, bg="#12161c")
        self.master_frame.pack(fill=tk.BOTH, expand=True)

        # -------------------------------------------------------------
        # PHONE PANE: Normal Phone Emulator Form Factor (~390x810)
        # -------------------------------------------------------------
        self.phone_frame = tk.Frame(self.master_frame, bg="#12161c", width=390)
        self.phone_frame.pack(side=tk.LEFT, fill=tk.Y, padx=(5, 5), pady=5)

        # Top Phone Header Bar
        top_bar = tk.Frame(self.phone_frame, bg="#12161c", height=30)
        top_bar.pack(fill=tk.X, padx=8, pady=(2, 4))

        tk.Label(
            top_bar,
            text="📱 Nokia 2660",
            font=("Helvetica", 11, "bold"),
            bg="#12161c",
            fg="#e6edf3"
        ).pack(side=tk.LEFT)

        self.status_badge = tk.Label(
            top_bar,
            text="● ON",
            font=("Helvetica", 9, "bold"),
            bg="#12161c",
            fg="#3fb950"
        )
        self.status_badge.pack(side=tk.LEFT, padx=6)

        # Header Control Buttons (Reboot, Pause, Drawer Toggle)
        self.btn_tools = tk.Button(
            top_bar,
            text="🛠️ Tools »",
            command=self._toggle_tools_drawer,
            bg="#21262d",
            fg="#58a6ff",
            font=("Helvetica", 9, "bold"),
            relief=tk.FLAT,
            padx=6,
            pady=1
        )
        self.btn_tools.pack(side=tk.RIGHT, padx=2)

        self.btn_reset = tk.Button(
            top_bar,
            text="🔄",
            command=self._reset_emu,
            bg="#21262d",
            fg="#c9d1d9",
            font=("Helvetica", 9),
            relief=tk.FLAT,
            padx=4,
            pady=1
        )
        self.btn_reset.pack(side=tk.RIGHT, padx=2)

        self.btn_run = tk.Button(
            top_bar,
            text="⏸",
            command=self._toggle_run,
            bg="#21262d",
            fg="#c9d1d9",
            font=("Helvetica", 9),
            relief=tk.FLAT,
            padx=4,
            pady=1
        )
        self.btn_run.pack(side=tk.RIGHT, padx=2)

        # Phone Canvas (Draws Clamshell Shell, Bezel, Display, Hinge, and Keys)
        self.phone_canvas = tk.Canvas(
            self.phone_frame,
            width=390,
            height=760,
            bg="#12161c",
            highlightthickness=0
        )
        self.phone_canvas.pack(fill=tk.BOTH, expand=True)

        self._draw_phone_shell()

        # -------------------------------------------------------------
        # COLLAPSIBLE DRAWER: Console, Registers, Payload Selection
        # -------------------------------------------------------------
        self.drawer_frame = tk.Frame(self.master_frame, bg="#161b22", width=420)

        # Payload Selector Bar
        bar = tk.LabelFrame(
            self.drawer_frame,
            text=" 🎮 Payload & Bootloader ",
            bg="#21262d",
            fg="#58a6ff",
            font=("Helvetica", 10, "bold")
        )
        bar.pack(fill=tk.X, padx=8, pady=4)

        row1 = tk.Frame(bar, bg="#21262d")
        row1.pack(fill=tk.X, padx=4, pady=3)

        tk.Label(row1, text="Payload:", bg="#21262d", fg="#c9d1d9", font=("Helvetica", 9)).pack(side=tk.LEFT)
        self.payload_var = tk.StringVar(value="Payload: baremetal_lcd.bin")
        self.payload_combo = ttk.Combobox(
            row1,
            textvariable=self.payload_var,
            state="readonly",
            width=22
        )
        self.payload_combo.pack(side=tk.LEFT, padx=4)

        btn_browse = tk.Button(
            row1,
            text="Browse...",
            command=self._browse_binary,
            bg="#30363d",
            fg="#c9d1d9",
            relief=tk.FLAT,
            font=("Helvetica", 9)
        )
        btn_browse.pack(side=tk.LEFT, padx=2)

        row2 = tk.Frame(bar, bg="#21262d")
        row2.pack(fill=tk.X, padx=4, pady=3)

        tk.Label(row2, text="Boot Key:", bg="#21262d", fg="#c9d1d9", font=("Helvetica", 9)).pack(side=tk.LEFT)
        self.bootkey_var = tk.StringVar(value="none")
        bk_menu = ttk.Combobox(
            row2,
            textvariable=self.bootkey_var,
            values=["none", "uart (Key 7)", "jtag (Key 3)"],
            state="readonly",
            width=14
        )
        bk_menu.pack(side=tk.LEFT, padx=4)

        # CPU Register Telemetry Box
        reg_box = tk.LabelFrame(
            self.drawer_frame,
            text=" ⚡ ARMv7-A CPU Registers ",
            bg="#21262d",
            fg="#58a6ff",
            font=("Helvetica", 10, "bold")
        )
        reg_box.pack(fill=tk.X, padx=8, pady=4)

        self.reg_labels = {}
        reg_grid = tk.Frame(reg_box, bg="#161b22")
        reg_grid.pack(fill=tk.X, padx=4, pady=3)

        regs = ["R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7", "R8", "R9", "R10", "R11", "R12", "SP", "LR", "PC", "CPSR"]
        for idx, r in enumerate(regs):
            row = idx // 4
            col = (idx % 4) * 2
            tk.Label(
                reg_grid,
                text=f"{r}:",
                bg="#161b22",
                fg="#8b949e",
                font=("Courier", 9, "bold")
            ).grid(row=row, column=col, sticky="w", padx=3, pady=1)
            val_lbl = tk.Label(
                reg_grid,
                text="0x00000000",
                bg="#161b22",
                fg="#39d353",
                font=("Courier", 9)
            )
            val_lbl.grid(row=row, column=col+1, sticky="w", padx=1, pady=1)
            self.reg_labels[r] = val_lbl

        # Serial UART Console (0x70100000)
        console_box = tk.LabelFrame(
            self.drawer_frame,
            text=" 🖥️ UART1 Serial Console (0x70100000) ",
            bg="#21262d",
            fg="#58a6ff",
            font=("Helvetica", 10, "bold")
        )
        console_box.pack(fill=tk.BOTH, expand=True, padx=8, pady=4)

        self.console_text = tk.Text(
            console_box,
            bg="#0d1117",
            fg="#58a6ff",
            font=("Courier", 9),
            wrap=tk.CHAR
        )
        self.console_text.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        scrollbar = ttk.Scrollbar(console_box, orient=tk.VERTICAL, command=self.console_text.yview)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        self.console_text.config(yscrollcommand=scrollbar.set)

        # Status Line
        self.status_var = tk.StringVar(value="Ready. Emulation active.")
        status_bar = tk.Label(
            self.drawer_frame,
            textvariable=self.status_var,
            bd=1,
            relief=tk.SUNKEN,
            anchor=tk.W,
            bg="#0d1117",
            fg="#8b949e",
            font=("Helvetica", 8)
        )
        status_bar.pack(side=tk.BOTTOM, fill=tk.X)

        # Global Keyboard Bindings
        self.bind("<KeyPress>", self._on_key_press)

        # Populate payload list
        self._populate_payload_list()

    def _draw_phone_shell(self):
        c = self.phone_canvas

        # 1. TOP LID (Screen Half)
        c.create_rectangle(15, 10, 375, 396, fill="#1c2430", outline="#303b4e", width=3)
        c.create_rectangle(22, 16, 368, 390, fill="#161c24", outline="#252d3a", width=1)

        # Top Earpiece Speaker Bar
        c.create_rectangle(165, 24, 225, 28, fill="#0d1117", outline="#30363d")

        # Nokia Logo
        c.create_text(195, 42, text="NOKIA", fill="#f0f6fc", font=("Helvetica", 11, "bold"))

        # Hardware Status Indicators (Flashlight Torch & Vibrator)
        self.torch_ind = c.create_oval(34, 32, 48, 46, fill="#30363d", outline="#484f58")
        c.create_text(41, 52, text="TORCH", fill="#8b949e", font=("Helvetica", 7, "bold"))

        self.vibr_ind = c.create_rectangle(342, 32, 356, 46, fill="#30363d", outline="#484f58")
        c.create_text(349, 52, text="VIBR", fill="#8b949e", font=("Helvetica", 7, "bold"))

        # Native 240x320 Display Screen Bezel
        sx0 = (390 - self.disp_w) // 2  # 75
        sy0 = 60
        c.create_rectangle(sx0 - 4, sy0 - 4, sx0 + self.disp_w + 4, sy0 + self.disp_h + 4, fill="#080c10", outline="#30363d", width=2)
        # Dedicated Screen Widget (Uses tk.Label for 100% reliable rendering on macOS Cocoa Tkinter)
        self.screen_label = tk.Label(self.phone_canvas, bg="#080c10", bd=0, highlightthickness=0)
        self.phone_canvas.create_window(sx0, sy0, anchor=tk.NW, window=self.screen_label, width=self.disp_w, height=self.disp_h)

        # 2. CENTER HINGE
        c.create_rectangle(35, 400, 355, 414, fill="#2d333b", outline="#444c56", width=2)
        c.create_line(170, 400, 170, 414, fill="#1c2128", width=2)
        c.create_line(220, 400, 220, 414, fill="#1c2128", width=2)

        # 3. BOTTOM BASE (Keypad Half)
        c.create_rectangle(15, 418, 375, 755, fill="#1c2430", outline="#303b4e", width=3)
        c.create_rectangle(22, 424, 368, 749, fill="#161c24", outline="#252d3a", width=1)

        # Build Full Nokia 2660 Keypad
        self._create_nokia_keypad()

        # Bottom Microphone Hole
        c.create_oval(193, 736, 197, 740, fill="#0d1117", outline="#30363d")

    def _create_nokia_keypad(self):
        # -------------------------------------------------------------
        # NAVIGATION CLUSTER (y: 430 - 526)
        # -------------------------------------------------------------
        # Left Softkey (LSK) & Call
        self._add_btn(36, 432, 72, 36, "— LSK", "LSK", "#21262d")
        self._add_btn(36, 482, 72, 40, "📞 CALL", "CALL", "#194226", fg="#3fb950")

        # 5-Way D-Pad: Up, Down, Left, Right, Center OK
        self._add_btn(160, 430, 70, 26, "▲", "UP", "#21262d")
        self._add_btn(120, 462, 34, 32, "◀", "LEFT", "#21262d")
        self._add_btn(160, 462, 70, 32, "● OK", "OK", "#1f6feb", fg="#ffffff")
        self._add_btn(236, 462, 34, 32, "▶", "RIGHT", "#21262d")
        self._add_btn(160, 500, 70, 26, "▼", "DOWN", "#21262d")

        # Right Softkey (RSK) & End/Power
        self._add_btn(282, 432, 72, 36, "RSK —", "RSK", "#21262d")
        self._add_btn(282, 482, 72, 40, "📴 END", "END", "#4c1d22", fg="#f85149")

        # -------------------------------------------------------------
        # 4x3 NUMERIC KEYPAD (y: 534 - 726)
        # -------------------------------------------------------------
        keys = [
            [("1", "1"), ("2 abc", "2"), ("3 def", "3")],
            [("4 ghi", "4"), ("5 jkl", "5"), ("6 mno", "6")],
            [("7 pqrs", "7"), ("8 tuv", "8"), ("9 wxyz", "9")],
            [("* +", "*"), ("0 ␣", "0"), ("# ⇧", "#")],
        ]

        for r_idx, row in enumerate(keys):
            ry = 534 + r_idx * 50
            for c_idx, (label, code) in enumerate(row):
                rx = 36 + c_idx * 110
                self._add_btn(rx, ry, 98, 42, label, code, "#21262d")

    def _add_btn(self, x, y, w, h, label, code, bg, fg="#c9d1d9"):
        btn = tk.Button(
            self.phone_canvas,
            text=label,
            bg=bg,
            fg=fg,
            activebackground="#388bfd",
            activeforeground="white",
            relief=tk.RAISED,
            bd=1,
            font=("Helvetica", 9, "bold" if code in ("OK", "CALL", "END") else "normal"),
            command=lambda: self._on_button_click(code)
        )
        self.btn_widgets[code] = (btn, bg)
        self.phone_canvas.create_window(x + w // 2, y + h // 2, window=btn, width=w, height=h)

    def _toggle_tools_drawer(self):
        if not self.drawer_open:
            self.drawer_frame.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True, padx=(0, 6), pady=6)
            self.geometry("820x820")
            self.btn_tools.config(text="« Phone Only", fg="#3fb950")
            self.drawer_open = True
        else:
            self.drawer_frame.pack_forget()
            self.geometry("400x820")
            self.btn_tools.config(text="🛠️ Tools »", fg="#58a6ff")
            self.drawer_open = False

    def _load_default_splash(self):
        img = Image.new("RGB", (self.disp_w, self.disp_h), color="#084360")
        d = ImageDraw.Draw(img)
        # Nokia Blue gradient bar
        d.rectangle([(0, 0), (self.disp_w, 42)], fill="#016cbb")
        d.text((45, 14), "NOKIA 2660 FLIP", fill="#ffffff")
        d.text((50, 140), "NOKIA 2660 FLIP", fill="#ffffff")
        d.text((55, 165), "STARTING CPU...", fill="#39d353")
        self._update_display_image(img)

    def _update_display_image(self, pil_img):
        self.screen_photo = ImageTk.PhotoImage(pil_img)
        if hasattr(self, 'screen_label') and self.screen_label.winfo_exists():
            self.screen_label.config(image=self.screen_photo)
            self.screen_label.image = self.screen_photo

    def _populate_payload_list(self):
        items = []
        if os.path.isdir(PAYLOAD_DIR):
            for f in sorted(os.listdir(PAYLOAD_DIR)):
                if f.endswith(".bin"):
                    items.append(f"Payload: {f}")
        if os.path.isdir(DUMP_DIR):
            for b in ["boot0.bin", "boot1.bin"]:
                if os.path.isfile(os.path.join(DUMP_DIR, b)):
                    items.append(f"Dump: {b}")

        self.payload_combo["values"] = items
        self.payload_combo.bind("<<ComboboxSelected>>", self._on_payload_selected)
        if "Payload: baremetal_lcd.bin" in items:
            self.payload_combo.set("Payload: baremetal_lcd.bin")
            self.current_binary = os.path.join(PAYLOAD_DIR, "baremetal_lcd.bin")
            self.current_profile = "payload"

    def _on_payload_selected(self, event):
        val = self.payload_var.get()
        if "boot0.bin" in val:
            self.current_binary = os.path.join(DUMP_DIR, "boot0.bin")
            self.current_profile = "boot0"
        elif "boot1.bin" in val:
            self.current_binary = os.path.join(DUMP_DIR, "boot1.bin")
            self.current_profile = "boot1"
        else:
            filename = val.replace("Payload: ", "")
            self.current_binary = os.path.join(PAYLOAD_DIR, filename)
            self.current_profile = "payload"

        if hasattr(self, 'console_text') and self.console_text is not None:
            self.console_text.insert(tk.END, f"[Payload] Selected: {os.path.basename(self.current_binary)}\n")
            self.console_text.see(tk.END)

        # Restart with the new binary
        if self.running:
            self._stop_emulation()
            self.after(150, self._start_emulation)

    def _browse_binary(self):
        path = filedialog.askopenfilename(
            title="Select Raw ARM Binary",
            filetypes=[("Raw Binary", "*.bin"), ("All Files", "*.*")]
        )
        if path:
            self.current_binary = path
            self.payload_var.set(os.path.basename(path))
            self.current_profile = "payload"
            if self.running:
                self._stop_emulation()
                self.after(150, self._start_emulation)

    def _on_button_click(self, code):
        # 1. Tactile Button Flash Feedback
        if code in self.btn_widgets:
            btn, orig_bg = self.btn_widgets[code]
            btn.config(bg="#388bfd", relief=tk.SUNKEN)
            self.after(120, lambda: btn.config(bg=orig_bg, relief=tk.RAISED) if btn.winfo_exists() else None)

        if hasattr(self, 'console_text') and self.console_text is not None:
            self.console_text.insert(tk.END, f"[Keypad] Key: {code}\n")
            self.console_text.see(tk.END)

        # 2. Thread-Safe Hardware Event Delivery
        self.key_event_queue.put(code)

    def _on_key_press(self, event):
        key_map = {
            '1': '1', '2': '2', '3': '3',
            '4': '4', '5': '5', '6': '6',
            '7': '7', '8': '8', '9': '9',
            '0': '0', '*': '*', '#': '#',
            'Return': 'OK', 'space': 'OK',
            'Up': 'UP', 'Down': 'DOWN', 'Left': 'LEFT', 'Right': 'RIGHT',
            'Escape': 'END', 'BackSpace': 'END', 'Delete': 'END',
            'q': 'LSK', 'F1': 'LSK',
            'e': 'RSK', 'F2': 'RSK',
            'c': 'CALL', 'C': 'CALL',
            'p': 'END', 'P': 'END',
            'plus': 'VOL_UP', 'equal': 'VOL_UP',
            'minus': 'VOL_DOWN', 'underscore': 'VOL_DOWN'
        }
        if event.keysym in key_map:
            self._on_button_click(key_map[event.keysym])

    def _toggle_run(self):
        if not self.running:
            self._start_emulation()
        else:
            self._stop_emulation()

    def _start_emulation(self):
        if not self.current_binary or not os.path.isfile(self.current_binary):
            return

        if self.emu_thread and self.emu_thread.is_alive():
            self._stop_emulation()

        self.running = True
        self.btn_run.config(text="⏸")
        self.status_badge.config(text="● ON", fg="#3fb950")
        self.status_var.set(f"Running {os.path.basename(self.current_binary)}...")

        # Extract Tkinter variables safely on the main thread before starting worker
        binary = self.current_binary
        profile = self.current_profile
        boot_key = self.bootkey_var.get() if hasattr(self, 'bootkey_var') else "none"

        self.emu_thread = threading.Thread(
            target=self._run_emu_worker,
            args=(binary, profile, boot_key),
            daemon=True
        )
        self.emu_thread.start()

    def _stop_emulation(self):
        self.running = False
        if self.active_emu and hasattr(self.active_emu, 'uc') and self.active_emu.uc:
            try:
                self.active_emu.uc.emu_stop()
            except Exception:
                pass
        if self.emu_thread and self.emu_thread.is_alive():
            self.emu_thread.join(timeout=0.6)

        self.btn_run.config(text="▶")
        self.status_badge.config(text="○ OFF", fg="#8b949e")
        self.status_var.set("Emulation stopped.")

    def _reset_emu(self):
        self._stop_emulation()
        if hasattr(self, 'console_text') and self.console_text is not None:
            self.console_text.delete("1.0", tk.END)
            self.console_text.insert(tk.END, "[Reset] Restarting CPU & peripherals...\n")
        self._load_default_splash()
        self.after(150, self._start_emulation)

    def _run_emu_worker(self, binary, profile, boot_key):
        base = 0x80100000 if profile == "payload" else 0x00006200
        entry = 0x80100200 if profile == "payload" else 0x00006420

        emu = UMS9117Emulator(trace=False, verbose=False)
        self.active_emu = emu

        if "uart" in boot_key:
            emu.set_boot_key("uart")
        elif "jtag" in boot_key:
            emu.set_boot_key("jtag")

        emu.load_binary(binary, base)

        # 1. Wire Framebuffer Refresh Callback
        def on_frame(raw_bytes):
            try:
                self.screen_queue.put_nowait(raw_bytes)
            except queue.Full:
                pass

        emu.on_frame_callback = on_frame

        # 2. Intercept UART Output to Console
        def uart_hook(uc, access, address, size, value, user_data):
            ch = chr(value & 0xFF)
            self.uart_queue.put(ch)

        emu.uc.hook_add(UC_HOOK_MEM_WRITE, uart_hook, begin=0x70100000, end=0x70100004)

        # 3. Intercept Flashlight LED & Haptic Vibrator
        def pmic_hook(uc, access, address, size, value, user_data):
            if address == 0x40608df4:
                on = bool(value & 0x8000)
                lvl = value & 0x0F
                self.hardware_events.put(("torch", on, lvl))
            elif address == 0x40608e00:
                on = bool(value & 0x8000)
                lvl = value & 0x07
                self.hardware_events.put(("vibr", on, lvl))

        emu.uc.hook_add(UC_HOOK_MEM_WRITE, pmic_hook, begin=0x40608df0, end=0x40608e08)

        # 4. Periodic CPU Register Telemetry & Safe Keypad Hook
        def code_hook(uc, address, size, user_data):
            emu.insn_count += 1

            # Process pending keypad events inside the emulation thread
            while not self.key_event_queue.empty():
                try:
                    k = self.key_event_queue.get_nowait()
                    emu.press_key(k)
                    # Hold key pressed for ~60,000 instructions (~2-3 display frames)
                    emu.key_release_step = emu.insn_count + 60000
                except queue.Empty:
                    break

            if getattr(emu, 'key_release_step', 0) and emu.insn_count >= emu.key_release_step:
                emu.release_key('')
                emu.key_release_step = 0

            if (emu.insn_count % 5000) == 0:
                regs = {
                    "R0": uc.reg_read(UC_ARM_REG_R0),
                    "R1": uc.reg_read(UC_ARM_REG_R1),
                    "R2": uc.reg_read(UC_ARM_REG_R2),
                    "R3": uc.reg_read(UC_ARM_REG_R3),
                    "R4": uc.reg_read(UC_ARM_REG_R4),
                    "R5": uc.reg_read(UC_ARM_REG_R5),
                    "R6": uc.reg_read(UC_ARM_REG_R6),
                    "R7": uc.reg_read(UC_ARM_REG_R7),
                    "R8": uc.reg_read(UC_ARM_REG_R8),
                    "R9": uc.reg_read(UC_ARM_REG_R9),
                    "R10": uc.reg_read(UC_ARM_REG_R10),
                    "R11": uc.reg_read(UC_ARM_REG_R11),
                    "R12": uc.reg_read(UC_ARM_REG_R12),
                    "SP": uc.reg_read(UC_ARM_REG_SP),
                    "LR": uc.reg_read(UC_ARM_REG_LR),
                    "PC": address,
                    "CPSR": uc.reg_read(UC_ARM_REG_CPSR),
                }
                try:
                    self.reg_queue.put_nowait(regs)
                except queue.Full:
                    pass

        emu.uc.hook_add(UC_HOOK_CODE, code_hook)

        # Run Continuous Emulation (max_insn=0, timeout_ms=0 for indefinite run)
        try:
            emu.run(entry, max_insn=0, timeout_ms=0)
        except Exception as e:
            self.uart_queue.put(f"\n[Fault] {e}\n")

        self.status_queue.put(("finished", f"Emulation finished ({emu.insn_count} instructions)."))

    def _poll_queues(self):
        try:
            if not self.winfo_exists():
                return
        except Exception:
            return

        # 1. Flush UART Output
        chars = []
        while not self.uart_queue.empty():
            try:
                chars.append(self.uart_queue.get_nowait())
            except queue.Empty:
                break
        if chars and hasattr(self, 'console_text') and self.console_text is not None:
            self.console_text.insert(tk.END, "".join(chars))
            self.console_text.see(tk.END)

        # 2. Hardware Indicator Events (LED Torch & Vibrator)
        while not self.hardware_events.empty():
            try:
                evt = self.hardware_events.get_nowait()
                if evt[0] == "torch":
                    on, lvl = evt[1], evt[2]
                    self.torch_on = on
                    color = "#ffd700" if on else "#30363d"
                    self.phone_canvas.itemconfig(self.torch_ind, fill=color)
                elif evt[0] == "vibr":
                    on, lvl = evt[1], evt[2]
                    self.vibr_on = on
                    color = "#39d353" if on else "#30363d"
                    self.phone_canvas.itemconfig(self.vibr_ind, fill=color)
            except queue.Empty:
                break

        # 3. Live Display Screen Refresh (Native 240x320 RGB565)
        latest_frame = None
        while not self.screen_queue.empty():
            try:
                latest_frame = self.screen_queue.get_nowait()
            except queue.Empty:
                break
        if latest_frame and len(latest_frame) == self.disp_w * self.disp_h * 2:
            try:
                img = Image.frombytes("RGB", (self.disp_w, self.disp_h), latest_frame, "raw", "BGR;16")
                self._update_display_image(img)
            except Exception:
                pass

        # 4. ARMv7-A CPU Register Monitor Update
        latest_regs = None
        while not self.reg_queue.empty():
            try:
                latest_regs = self.reg_queue.get_nowait()
            except queue.Empty:
                break
        if latest_regs and hasattr(self, 'reg_labels'):
            for reg_name, val in latest_regs.items():
                if reg_name in self.reg_labels:
                    self.reg_labels[reg_name].config(text=f"0x{val:08x}")

        # 5. Emulation Status Updates (Handled safely on main thread)
        while not self.status_queue.empty():
            try:
                evt, msg = self.status_queue.get_nowait()
                if evt == "finished":
                    self.running = False
                    self.btn_run.config(text="▶")
                    self.status_badge.config(text="○ OFF", fg="#8b949e")
                    self.status_var.set(msg)
            except queue.Empty:
                break

        # Poll again in 30ms (~33 FPS)
        try:
            if self.winfo_exists():
                self.after(30, self._poll_queues)
        except Exception:
            pass


def main():
    app = NokiaGUIEmulator()
    app.mainloop()


if __name__ == "__main__":
    main()
