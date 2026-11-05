#!/usr/bin/env python3
"""
nokia_gui_emu.py — Interactive GUI Emulator with Nokia 2660 Skin & Live Display

Emulates the Nokia 2660 Flip (TA-1480 / Unisoc UMS9117) with:
- Realistic interactive Nokia 2660 clamshell phone skin
- 240x320 live QVGA display screen (RGB565 via Pillow)
- Clickable keypad buttons & PC keyboard mapping
- Hardware indicators: Flashlight Torch LED and Haptic Vibration pulse
- Real-time UART1 serial console drawer (0x70100000)
- Live ARMv7-A register monitor (r0-r12, SP, LR, PC, CPSR)
- Fast payload loader menu (hello_world, keypad_test, flashlight, boot0, etc.)
"""

import sys
import os

# Suppress macOS Tkinter deprecation warning
os.environ["TK_SILENCE_DEPRECATION"] = "1"

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
SPLASH_BMP = os.path.join(os.path.dirname(__file__), "nokia_boot_splash.bmp")


class NokiaGUIEmulator(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Nokia 2660 Flip (TA-1480) — Hardware Emulator")
        self.geometry("980x760")
        self.minsize(880, 680)
        self.configure(bg="#1a1e24")

        # Emulation State
        self.emu = None
        self.active_emu = None
        self.emu_thread = None
        self.running = False
        self.current_binary = None
        self.current_profile = "payload"
        self.key_event_queue = queue.Queue()
        self.uart_queue = queue.Queue()
        self.hardware_events = queue.Queue()
        self.screen_queue = queue.Queue(maxsize=10)
        self.reg_queue = queue.Queue(maxsize=10)
        self.status_queue = queue.Queue()

        # Virtual Screen Settings
        self.disp_w = 240
        self.disp_h = 320
        self.disp_scale = 1.25  # 300x400 display area
        self.screen_img = None
        self.screen_photo = None
        self.raw_fb = bytearray(self.disp_w * self.disp_h * 2)

        # Hardware Indicators
        self.torch_on = False
        self.torch_level = 0
        self.vibr_on = False

        self._build_ui()
        self._load_default_splash()
        self._poll_queues()

    def _build_ui(self):
        # Main Layout: Left = Phone Chassis / Skin, Right = Telemetry & Console
        main_pane = tk.PanedWindow(self, orient=tk.HORIZONTAL, bg="#1a1e24", sashwidth=4)
        main_pane.pack(fill=tk.BOTH, expand=True, padx=8, pady=8)

        # -------------------------------------------------------------
        # LEFT PANE: Nokia 2660 Flip Chassis Skin
        # -------------------------------------------------------------
        phone_frame = tk.Frame(main_pane, bg="#1a1e24")
        main_pane.add(phone_frame, minsize=420)

        # Canvas for Phone Shell & Keypad
        self.phone_canvas = tk.Canvas(
            phone_frame, width=420, height=720, bg="#1a1e24", highlightthickness=0
        )
        self.phone_canvas.pack(fill=tk.BOTH, expand=True)

        self._draw_phone_shell()

        # -------------------------------------------------------------
        # RIGHT PANE: Controls, Registers, and Serial Console
        # -------------------------------------------------------------
        control_frame = tk.Frame(main_pane, bg="#21262d")
        main_pane.add(control_frame, minsize=460)

        # Top Control Bar (Payload selector, Run/Pause buttons)
        bar = tk.LabelFrame(control_frame, text=" 🎮 Firmware & Payload Control ", bg="#21262d", fg="#58a6ff", font=("Helvetica", 11, "bold"))
        bar.pack(fill=tk.X, padx=10, pady=6)

        row1 = tk.Frame(bar, bg="#21262d")
        row1.pack(fill=tk.X, padx=6, pady=4)

        tk.Label(row1, text="Payload:", bg="#21262d", fg="#c9d1d9").pack(side=tk.LEFT)
        self.payload_var = tk.StringVar()
        self.payload_combo = ttk.Combobox(
            row1, textvariable=self.payload_var, state="readonly", width=28
        )
        self.payload_combo.pack(side=tk.LEFT, padx=6)

        btn_browse = tk.Button(row1, text="Browse...", command=self._browse_binary, bg="#30363d", fg="#c9d1d9", relief=tk.FLAT)
        btn_browse.pack(side=tk.LEFT, padx=4)

        row2 = tk.Frame(bar, bg="#21262d")
        row2.pack(fill=tk.X, padx=6, pady=4)

        self.btn_run = tk.Button(row2, text="▶ Run Payload", command=self._toggle_run, bg="#238636", fg="white", font=("Helvetica", 10, "bold"), relief=tk.RAISED, width=12)
        self.btn_run.pack(side=tk.LEFT, padx=4)

        self.btn_demo = tk.Button(row2, text="📺 Live LCD Demo", command=self._launch_lcd_demo, bg="#1f6feb", fg="white", font=("Helvetica", 10, "bold"), relief=tk.RAISED, width=14)
        self.btn_demo.pack(side=tk.LEFT, padx=4)

        self.btn_reset = tk.Button(row2, text="🔄 Reset", command=self._reset_emu, bg="#da3633", fg="white", font=("Helvetica", 10, "bold"), relief=tk.RAISED, width=8)
        self.btn_reset.pack(side=tk.LEFT, padx=4)

        tk.Label(row2, text="Boot Key:", bg="#21262d", fg="#c9d1d9").pack(side=tk.LEFT, padx=(12, 2))
        self.bootkey_var = tk.StringVar(value="none")
        bk_menu = ttk.Combobox(row2, textvariable=self.bootkey_var, values=["none", "uart (Key 7)", "jtag (Key 3)"], state="readonly", width=12)
        bk_menu.pack(side=tk.LEFT)

        # CPU Register Telemetry Box
        reg_box = tk.LabelFrame(control_frame, text=" ⚡ ARMv7-A CPU Registers ", bg="#21262d", fg="#58a6ff", font=("Helvetica", 10, "bold"))
        reg_box.pack(fill=tk.X, padx=10, pady=4)

        self.reg_labels = {}
        reg_grid = tk.Frame(reg_box, bg="#161b22")
        reg_grid.pack(fill=tk.X, padx=4, pady=4)

        regs = ["R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7", "R8", "R9", "R10", "R11", "R12", "SP", "LR", "PC", "CPSR"]
        for idx, r in enumerate(regs):
            row = idx // 4
            col = (idx % 4) * 2
            tk.Label(reg_grid, text=f"{r}:", bg="#161b22", fg="#8b949e", font=("Courier", 10, "bold")).grid(row=row, column=col, sticky="w", padx=4, pady=1)
            val_lbl = tk.Label(reg_grid, text="0x00000000", bg="#161b22", fg="#39d353", font=("Courier", 10))
            val_lbl.grid(row=row, column=col+1, sticky="w", padx=2, pady=1)
            self.reg_labels[r] = val_lbl

        # Serial UART Console (0x70100000)
        console_box = tk.LabelFrame(control_frame, text=" 🖥️ UART1 Serial Console (0x70100000 @ 115200 8N1) ", bg="#21262d", fg="#58a6ff", font=("Helvetica", 10, "bold"))
        console_box.pack(fill=tk.BOTH, expand=True, padx=10, pady=6)

        self.console_text = tk.Text(console_box, bg="#0d1117", fg="#58a6ff", font=("Courier", 10), wrap=tk.CHAR)
        self.console_text.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        scrollbar = ttk.Scrollbar(console_box, orient=tk.VERTICAL, command=self.console_text.yview)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        self.console_text.config(yscrollcommand=scrollbar.set)

        # Status Bar
        self.status_var = tk.StringVar(value="Ready. Select payload and click Run.")
        status_bar = tk.Label(self, textvariable=self.status_var, bd=1, relief=tk.SUNKEN, anchor=tk.W, bg="#0d1117", fg="#8b949e", font=("Helvetica", 9))
        status_bar.pack(side=tk.BOTTOM, fill=tk.X)

        # Keyboard Bindings
        self.bind("<Key>", self._on_key_press)

        # Populate payload list after all console and control widgets are built
        self._populate_payload_list()

    def _draw_phone_shell(self):
        c = self.phone_canvas
        w, h = 420, 720

        # Outer Flip Chassis (Curved Clamshell Silhouette)
        c.create_rectangle(30, 20, 390, 700, fill="#1c2430", outline="#2f3b4e", width=4)
        c.create_rectangle(38, 28, 382, 692, fill="#232e3d", outline="#1c2430", width=2)

        # Earpiece Grill & Nokia Brand
        c.create_rectangle(170, 38, 250, 43, fill="#0d1117", outline="#444d56")
        c.create_text(210, 56, text="NOKIA", fill="#ffffff", font=("Helvetica", 11, "bold"))

        # Screen Bezel Window (240x320 scaled to 300x400)
        sw, sh = int(self.disp_w * self.disp_scale), int(self.disp_h * self.disp_scale)
        sx0 = (420 - sw) // 2
        sy0 = 70
        c.create_rectangle(sx0 - 6, sy0 - 6, sx0 + sw + 6, sy0 + sh + 6, fill="#0d1117", outline="#30363d", width=3)
        self.screen_canvas_id = c.create_image(sx0, sy0, anchor=tk.NW)

        # Hardware Indicators (Flashlight Torch & Haptics)
        self.torch_ind = c.create_oval(sx0 - 24, sy0 + 10, sx0 - 8, sy0 + 26, fill="#30363d", outline="#444d56")
        c.create_text(sx0 - 16, sy0 + 32, text="LED", fill="#8b949e", font=("Helvetica", 7, "bold"))

        self.vibr_ind = c.create_rectangle(sx0 + sw + 8, sy0 + 10, sx0 + sw + 24, sy0 + 26, fill="#30363d", outline="#444d56")
        c.create_text(sx0 + sw + 16, sy0 + 32, text="VIBR", fill="#8b949e", font=("Helvetica", 7, "bold"))

        # Clamshell Hinge Line
        hy = sy0 + sh + 12
        c.create_rectangle(50, hy, 370, hy + 4, fill="#161b22", outline="#30363d")

        # Interactive Keypad Section
        ky0 = hy + 12
        self._create_keypad_buttons(ky0)

    def _create_keypad_buttons(self, y0):
        c = self.phone_canvas
        btn_w, btn_h = 76, 28
        spacing_x = 12
        spacing_y = 6
        start_x = 65

        # Function Keys Row 1: Softkeys & D-Pad Top
        # [ LSK ]   [ ^ ]   [ RSK ]
        self._add_btn(start_x, y0, btn_w, btn_h, "— (LSK)", "LSK", "#21262d")
        self._add_btn(start_x + btn_w + spacing_x, y0, btn_w, btn_h, "▲", "UP", "#161b22")
        self._add_btn(start_x + (btn_w + spacing_x) * 2, y0, btn_w, btn_h, "— (RSK)", "RSK", "#21262d")

        # Function Keys Row 2: Call, OK, End
        # [ CALL ]  [ OK ]  [ END ]
        y1 = y0 + btn_h + spacing_y
        self._add_btn(start_x, y1, btn_w, btn_h, "📞 CALL", "CALL", "#1a4627", fg="#3fb950")
        self._add_btn(start_x + btn_w + spacing_x, y1, btn_w, btn_h, "● OK", "OK", "#1f6feb", fg="white")
        self._add_btn(start_x + (btn_w + spacing_x) * 2, y1, btn_w, btn_h, "📴 END", "END", "#5a1e20", fg="#f85149")

        # Numeric Keypad 4x3 Grid
        keys = [
            [("1", "1"), ("2 abc", "2"), ("3 def", "3")],
            [("4 ghi", "4"), ("5 jkl", "5"), ("6 mno", "6")],
            [("7 pqrs", "7"), ("8 tuv", "8"), ("9 wxyz", "9")],
            [("* +", "*"), ("0 _", "0"), ("# ⇧", "#")],
        ]

        for r_idx, row in enumerate(keys):
            ry = y1 + btn_h + spacing_y + r_idx * (btn_h + spacing_y)
            for c_idx, (label, code) in enumerate(row):
                rx = start_x + c_idx * (btn_w + spacing_x)
                self._add_btn(rx, ry, btn_w, btn_h, label, code, "#21262d")

    def _add_btn(self, x, y, w, h, label, code, bg, fg="#c9d1d9"):
        btn = tk.Button(
            self.phone_canvas,
            text=label,
            bg=bg,
            fg=fg,
            activebackground="#58a6ff",
            activeforeground="white",
            font=("Helvetica", 9, "bold"),
            relief=tk.RAISED,
            command=lambda: self._on_button_click(code)
        )
        self.phone_canvas.create_window(x + w//2, y + h//2, window=btn, width=w, height=h)

    def _load_default_splash(self):
        """Render initial splash screen (Nokia Connecting Hands) or fallback canvas."""
        if os.path.isfile(SPLASH_BMP):
            try:
                img = Image.open(SPLASH_BMP)
                self._update_display_image(img)
                return
            except Exception:
                pass

        # Fallback dark background with Nokia text
        img = Image.new("RGB", (self.disp_w, self.disp_h), color="#084360")
        d = ImageDraw.Draw(img)
        d.text((35, 140), "NOKIA 2660 FLIP", fill="#ffffff")
        d.text((45, 160), "EMULATOR READY", fill="#39d353")
        self._update_display_image(img)

    def _update_display_image(self, pil_img):
        sw, sh = int(self.disp_w * self.disp_scale), int(self.disp_h * self.disp_scale)
        scaled = pil_img.resize((sw, sh), Image.NEAREST)
        self.screen_photo = ImageTk.PhotoImage(scaled)
        self.phone_canvas.itemconfig(self.screen_canvas_id, image=self.screen_photo)

    def _populate_payload_list(self):
        choices = []
        other_payloads = []
        if os.path.isdir(PAYLOAD_DIR):
            for f in sorted(os.listdir(PAYLOAD_DIR)):
                if f.endswith(".bin"):
                    entry = f"Payload: {f}"
                    if f == "baremetal_lcd.bin":
                        choices.append(entry) # Prioritize LCD Demo at top of list
                    else:
                        other_payloads.append(entry)
        choices.extend(other_payloads)
        if os.path.isfile(os.path.join(DUMP_DIR, "boot0.bin")):
            choices.append("Bootloader: boot0.bin (IRAM)")
        if os.path.isfile(os.path.join(DUMP_DIR, "boot1.bin")):
            choices.append("Bootloader: boot1.bin (DDR)")

        self.payload_combo['values'] = choices
        if choices:
            self.payload_combo.current(0)
            self._on_payload_selected(None)

        self.payload_combo.bind("<<ComboboxSelected>>", self._on_payload_selected)

    def _on_payload_selected(self, event):
        val = self.payload_var.get()
        msg = None
        if "boot0.bin" in val:
            self.current_binary = os.path.join(DUMP_DIR, "boot0.bin")
            self.current_profile = "boot0"
            msg = "[Note] boot0.bin is the stage 1 bootloader (IRAM setup & DDR calibration only; no LCD).\n"
        elif "boot1.bin" in val:
            self.current_binary = os.path.join(DUMP_DIR, "boot1.bin")
            self.current_profile = "boot1"
        else:
            filename = val.replace("Payload: ", "")
            self.current_binary = os.path.join(PAYLOAD_DIR, filename)
            self.current_profile = "payload"
            if filename == "baremetal_lcd.bin":
                msg = "[Ready] baremetal_lcd.bin: Click '▶ Run Payload' or '📺 Live LCD Demo' for 240x320 screen animation!\n"
            elif filename == "baremetal_asm.bin":
                msg = "[Note] baremetal_asm.bin tests CPU loop & mailbox markers only (no LCD code).\n"
        
        if msg and hasattr(self, 'console_text') and self.console_text is not None:
            self.console_text.insert(tk.END, msg)
            self.console_text.see(tk.END)

    def _launch_lcd_demo(self):
        lcd_payload = os.path.join(PAYLOAD_DIR, "baremetal_lcd.bin")
        if not os.path.isfile(lcd_payload):
            messagebox.showerror("Error", f"baremetal_lcd.bin not found in {PAYLOAD_DIR}")
            return
        self.payload_var.set("Payload: baremetal_lcd.bin")
        self.current_binary = lcd_payload
        self.current_profile = "payload"
        if self.running:
            self._stop_emulation()
            self.after(200, self._start_emulation)
        else:
            self._start_emulation()

    def _browse_binary(self):
        path = filedialog.askopenfilename(
            title="Select Raw ARM Binary",
            filetypes=[("Raw Binary", "*.bin"), ("All Files", "*.*")]
        )
        if path:
            self.current_binary = path
            self.payload_var.set(os.path.basename(path))
            self.current_profile = "payload"

    def _on_button_click(self, code):
        self.console_text.insert(tk.END, f"[Keypad Input] Key: {code}\n")
        self.console_text.see(tk.END)
        # Thread-safe queue delivery: worker thread handles hardware injection
        self.key_event_queue.put(code)

    def _on_key_press(self, event):
        key_map = {
            '1': '1', '2': '2', '3': '3',
            '4': '4', '5': '5', '6': '6',
            '7': '7', '8': '8', '9': '9',
            '0': '0', '*': '*', '#': '#',
            'Return': 'OK', 'space': 'OK',
            'Up': 'UP', 'Down': 'DOWN', 'Left': 'LEFT', 'Right': 'RIGHT',
            'Escape': 'END', 'BackSpace': 'END',
            'q': 'LSK', 'e': 'RSK', 'c': 'CALL'
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
            messagebox.showerror("Error", "Please select a valid binary payload or loader first.")
            return

        # Ensure any previous emulation thread is stopped cleanly before starting
        if self.emu_thread and self.emu_thread.is_alive():
            self._stop_emulation()

        self.running = True
        self.btn_run.config(text="⏸ Pause", bg="#d29922")
        self.status_var.set(f"Running {os.path.basename(self.current_binary)}...")

        # Launch emulation worker thread
        self.emu_thread = threading.Thread(target=self._run_emu_worker, daemon=True)
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
        self.btn_run.config(text="▶ Run Payload", bg="#238636")
        self.status_var.set("Emulation stopped.")

    def _reset_emu(self):
        self._stop_emulation()
        self.console_text.delete("1.0", tk.END)
        self.console_text.insert(tk.END, "[Emulator Reset] Hardware reset asserted.\n")
        self._load_default_splash()

    def _run_emu_worker(self):
        base = 0x80100000 if self.current_profile == "payload" else 0x00006200
        entry = 0x80100200 if self.current_profile == "payload" else 0x00006420

        emu = UMS9117Emulator(trace=False, verbose=False)
        self.active_emu = emu

        # Set Boot key mode
        bk = self.bootkey_var.get()
        if "uart" in bk:
            emu.set_boot_key("uart")
        elif "jtag" in bk:
            emu.set_boot_key("jtag")

        emu.load_binary(self.current_binary, base)

        # 1. Wire Framebuffer Refresh Callback
        def on_frame(raw_bytes):
            try:
                self.screen_queue.put_nowait(raw_bytes)
            except queue.Full:
                pass

        emu.on_frame_callback = on_frame

        # 2. Intercept UART to GUI
        def uart_hook(uc, access, address, size, value, user_data):
            ch = chr(value & 0xFF)
            self.uart_queue.put(ch)

        emu.uc.hook_add(UC_HOOK_MEM_WRITE, uart_hook, begin=0x70100000, end=0x70100004)

        # 3. Intercept Flashlight LED & Haptics to GUI
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

            # Process pending keypad events safely within the emulation thread
            while not self.key_event_queue.empty():
                try:
                    k = self.key_event_queue.get_nowait()
                    emu.press_key(k)
                    emu.key_release_step = emu.insn_count + 5000
                except queue.Empty:
                    break

            if getattr(emu, 'key_release_step', 0) and emu.insn_count >= emu.key_release_step:
                emu.release_key('')
                emu.key_release_step = 0

            if (emu.insn_count % 2500) == 0:
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

        # Run Emulation
        try:
            emu.run(entry, max_insn=20000000, timeout_ms=30000)
        except Exception as e:
            self.uart_queue.put(f"\n[Fault] {e}\n")

        # Sample final registers on exit
        try:
            regs = {
                "R0": emu.uc.reg_read(UC_ARM_REG_R0),
                "R1": emu.uc.reg_read(UC_ARM_REG_R1),
                "R2": emu.uc.reg_read(UC_ARM_REG_R2),
                "R3": emu.uc.reg_read(UC_ARM_REG_R3),
                "R4": emu.uc.reg_read(UC_ARM_REG_R4),
                "R5": emu.uc.reg_read(UC_ARM_REG_R5),
                "R6": emu.uc.reg_read(UC_ARM_REG_R6),
                "R7": emu.uc.reg_read(UC_ARM_REG_R7),
                "R8": emu.uc.reg_read(UC_ARM_REG_R8),
                "R9": emu.uc.reg_read(UC_ARM_REG_R9),
                "R10": emu.uc.reg_read(UC_ARM_REG_R10),
                "R11": emu.uc.reg_read(UC_ARM_REG_R11),
                "R12": emu.uc.reg_read(UC_ARM_REG_R12),
                "SP": emu.uc.reg_read(UC_ARM_REG_SP),
                "LR": emu.uc.reg_read(UC_ARM_REG_LR),
                "PC": emu.uc.reg_read(UC_ARM_REG_PC),
                "CPSR": emu.uc.reg_read(UC_ARM_REG_CPSR),
            }
            self.reg_queue.put_nowait(regs)
        except Exception:
            pass

        # Read mailbox markers on finish
        mb = emu.read_mailbox(0x80700100)
        if mb and any(mb):
            m1, m2, m3, counter, status = mb
            self.uart_queue.put(
                f"\n=== Mailbox Markers ===\n"
                f"  Marker 1: 0x{m1:08x}\n"
                f"  Marker 2: 0x{m2:08x}\n"
                f"  Marker 3: 0x{m3:08x}\n"
                f"  Counter:  {counter}\n"
                f"  Status:   0x{status:08x}\n"
            )

        # Post finish status to queue (main thread updates Tkinter widgets safely)
        self.status_queue.put(("finished", f"Emulation finished ({emu.insn_count} instructions)."))

    def _poll_queues(self):
        # 1. Flush UART Output
        chars = []
        while not self.uart_queue.empty():
            try:
                chars.append(self.uart_queue.get_nowait())
            except queue.Empty:
                break
        if chars:
            self.console_text.insert(tk.END, "".join(chars))
            self.console_text.see(tk.END)

        # 2. Hardware Indicator Events
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

        # 3. Live Display Screen Refresh
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
        if latest_regs:
            for reg_name, val in latest_regs.items():
                if reg_name in self.reg_labels:
                    self.reg_labels[reg_name].config(text=f"0x{val:08x}")

        # 5. Emulation Status Updates (Handled safely on Tkinter main thread)
        while not self.status_queue.empty():
            try:
                evt, msg = self.status_queue.get_nowait()
                if evt == "finished":
                    self.running = False
                    self.btn_run.config(text="▶ Run Payload", bg="#238636")
                    self.status_var.set(msg)
            except queue.Empty:
                break

        # Poll again in 30ms (~33 FPS)
        self.after(30, self._poll_queues)


def main():
    app = NokiaGUIEmulator()
    app.mainloop()


if __name__ == "__main__":
    main()
