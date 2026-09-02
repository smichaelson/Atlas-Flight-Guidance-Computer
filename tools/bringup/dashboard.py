"""Atlas PCB bring-up dashboard: local desktop UI, no network service.

Major functions: Dashboard.connect opens only an explicitly chosen CDC port;
send issues one allowlisted command without retries; poll displays real bounded
JSON observations; record writes opt-in local evidence; main supports inert demo.
Run instructions and electrical prerequisites are in docs/startup.md.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import time
import tkinter as tk
from tkinter import filedialog, messagebox, scrolledtext, ttk

from protocol import Decoder, MODULES, Session, observations


class Dashboard:
    """One UI/serial owner with bounded polling; no background hardware workers."""

    def __init__(self, root: tk.Tk, demo: bool = False) -> None:
        """@brief Construct an initially disconnected UI. @param root Tk window."""
        self.root, self.demo = root, demo
        self.serial = None
        self.session, self.decoder = Session(), Decoder()
        self.log = None
        self.log_bytes = 0
        self.last_paint = 0.0
        self.started = time.monotonic()
        self.events = 0
        self.port_map: dict[str, str] = {}
        self.confirmed = tk.BooleanVar(value=False)
        self.port = tk.StringVar()
        self.banner = tk.StringVar(value="DISCONNECTED — measurements unknown")
        self.footer = tk.StringVar(value="Local only. No automatic tests, file writes or radio transmissions.")
        self.identity = tk.StringVar(value="Awaiting bring-up firmware handshake")
        self.root.title("Atlas | PCB bring-up" + (" | SIMULATED" if demo else ""))
        self.root.geometry("1160x820")
        self.root.minsize(980, 680)
        style = ttk.Style(root)
        style.theme_use("clam")
        style.configure("TLabel", font=("Segoe UI", 10))
        style.configure("TButton", padding=(9, 6))
        style.configure("Treeview", rowheight=31, font=("Segoe UI", 10))
        style.configure("Treeview.Heading", font=("Segoe UI", 10, "bold"))
        style.configure("Title.TLabel", font=("Segoe UI", 22, "bold"))
        shell = ttk.Frame(root, padding=18)
        shell.pack(fill="both", expand=True)
        ttk.Label(shell, text="ATLAS / PCB BRING-UP", style="Title.TLabel").pack(anchor="w")
        ttk.Label(shell, text="Bench diagnostics · PWM and pyro inhibited · No flight/control algorithm").pack(anchor="w", pady=(0, 12))
        bar = ttk.Frame(shell)
        bar.pack(fill="x")
        self.port_box = ttk.Combobox(bar, textvariable=self.port, state="readonly", width=48)
        self.port_box.pack(side="left", padx=(0, 6))
        for text, callback in (("Refresh ports", self.refresh_ports), ("Connect", self.connect),
                               ("Disconnect", self.disconnect), ("Identify", lambda: self.send("hello"))):
            ttk.Button(bar, text=text, command=callback).pack(side="left", padx=3)
        ttk.Label(shell, textvariable=self.banner, foreground="#934500").pack(anchor="w", pady=(12, 2))
        ttk.Label(shell, textvariable=self.identity).pack(anchor="w")
        ttk.Checkbutton(shell, variable=self.confirmed,
                        text="Power/USB checks complete; J5 OPEN; pyro/motors disconnected; radio absent OR later-stage setup verified.").pack(anchor="w", pady=10)
        tabs = ttk.Notebook(shell)
        tabs.pack(fill="both", expand=True)
        overview = ttk.Frame(tabs, padding=10)
        actions = ttk.Frame(tabs, padding=14)
        evidence = ttk.Frame(tabs, padding=10)
        tabs.add(overview, text="  Observations  ")
        tabs.add(actions, text="  Explicit tests  ")
        tabs.add(evidence, text="  Raw evidence & notes  ")
        ttk.Label(overview, text="RESPONDING is communication evidence, not calibration or electrical qualification. Ages are MCU milliseconds.").pack(anchor="w", pady=(0, 8))
        table_frame = ttk.Frame(overview)
        table_frame.pack(fill="both", expand=True)
        self.table = ttk.Treeview(table_frame, columns=("system", "state", "age", "measurement"), show="headings")
        for key, title, width in (("system", "System", 175), ("state", "Evidence", 205),
                                   ("age", "Age ms", 65), ("measurement", "Measurement / counters", 590)):
            self.table.heading(key, text=title)
            self.table.column(key, width=width, minwidth=width if key != "measurement" else 200, stretch=key == "measurement")
        self.table.pack(side="left", fill="both", expand=True)
        scroll = ttk.Scrollbar(table_frame, orient="vertical", command=self.table.yview)
        scroll.pack(side="right", fill="y")
        self.table.configure(yscrollcommand=scroll.set)
        for tag, color in (("good", "#176646"), ("warn", "#8C4800"), ("bad", "#AE2020"), ("unknown", "#657181")):
            self.table.tag_configure(tag, foreground=color)
        self.table.bind("<<TreeviewSelect>>", self.select_row)
        self.selected = tk.StringVar(value="Select a row for its full measurement text.")
        ttk.Label(overview, textvariable=self.selected, wraplength=1000).pack(fill="x", pady=8)
        self._actions(actions)
        self.raw = scrolledtext.ScrolledText(evidence, height=17, font=("Consolas", 9), state="disabled")
        self.raw.pack(fill="both", expand=True)
        self.event_view = scrolledtext.ScrolledText(evidence, height=6, font=("Consolas", 9), state="disabled")
        self.event_view.pack(fill="x", pady=8)
        log_bar = ttk.Frame(evidence)
        log_bar.pack(fill="x")
        ttk.Button(log_bar, text="Start new evidence log", command=self.start_log).pack(side="left")
        ttk.Button(log_bar, text="Stop log", command=self.stop_log).pack(side="left", padx=8)
        self.note = tk.StringVar()
        ttk.Entry(log_bar, textvariable=self.note).pack(side="left", fill="x", expand=True)
        ttk.Button(log_bar, text="Record bench note", command=self.add_note).pack(side="left", padx=8)
        ttk.Label(shell, textvariable=self.footer, wraplength=1080).pack(fill="x", pady=(10, 0))
        self.root.protocol("WM_DELETE_WINDOW", self.close)
        if demo:
            import demo as fixture
            self.session.accept(fixture.hello(), time.monotonic())
            self.port.set("SIMULATED — serial disabled")
        self.root.after(20, self.poll)

    def _buttons(self, parent: ttk.Frame, row: int, title: str, buttons: list[tuple[str, str]]) -> None:
        """@brief Add one named row of explicit controls. @param buttons Label/verb pairs."""
        ttk.Label(parent, text=title).grid(row=row, column=0, sticky="w", pady=7, padx=(0, 14))
        frame = ttk.Frame(parent)
        frame.grid(row=row, column=1, sticky="w")
        for label, verb in buttons:
            ttk.Button(frame, text=label, command=lambda v=verb: self.send(v)).pack(side="left", padx=(0, 5))

    def _actions(self, parent: ttk.Frame) -> None:
        """@brief Build safe, explicit controls; optional fixtures have confirmations."""
        self._buttons(parent, 0, "1 · Probe once per boot", [(m.upper(), f"probe {m}") for m in MODULES[:4]])
        self._buttons(parent, 1, "", [(m.upper(), f"probe {m}") for m in MODULES[4:7]])
        self._buttons(parent, 2, "2 · Indicators", [("Channel 1", "led 1"), ("Channel 2", "led 2"),
                      ("Channel 4", "led 4"), ("200 ms beep", "beep"), ("Indicators off", "stop")])
        self._buttons(parent, 3, "3 · SD card", [("Mount", "sd mount"), ("Read fixture", "sd read"),
                      ("NEW write / compare", "sd test"), ("Unmount", "sd unmount")])
        ttk.Button(parent, text="Set RTC to laptop UTC", command=self.utc).grid(row=4, column=1, sticky="w", pady=7)
        self._buttons(parent, 5, "4 · BLE peer test", [("Volatile SPS profile", "ble profile"), ("Data mode", "ble data"),
                      ("Transmit fixed text", "ble ping"), ("Command mode", "ble command")])
        gpio = [(str(i), f"gpio {i}") for i in range(1, 8)] + [("All low", "gpio 0")]
        self._buttons(parent, 6, "5 · Logic GPIO (1 s)", gpio)
        self._buttons(parent, 7, "Later · Wired fixtures", [("UART loopback", "uart"), ("SPI loopback", "spi")])
        row = ttk.Frame(parent)
        row.grid(row=8, column=1, sticky="w", pady=7)
        ttk.Label(row, text="Known I2C 7-bit address / register (decimal): ").pack(side="left")
        self.address, self.register = tk.StringVar(value=""), tk.StringVar(value="")
        ttk.Entry(row, textvariable=self.address, width=5).pack(side="left")
        ttk.Entry(row, textvariable=self.register, width=5).pack(side="left", padx=5)
        ttk.Button(row, text="Read one byte", command=self.i2c).pack(side="left")
        self._buttons(parent, 9, "Later · RFD900x", [("Transport init", "probe radio"), ("Read identity", "radio id"),
                      ("Transmit fixed text", "radio ping")])
        ttk.Label(parent, text="One operation at a time. No automatic retry. See startup.md for fixtures, expected evidence and fault recovery.\n"
                  "SD test refuses an existing ATLASCHK.TST. No format/erase command. There is deliberately no PWM/pyro enable or fire control.",
                  wraplength=1000).grid(row=10, column=0, columnspan=2, sticky="w", pady=16)

    def refresh_ports(self) -> None:
        """@brief Enumerate COM ports only; never select or open one automatically."""
        if self.demo:
            return
        try:
            from serial.tools import list_ports
            self.port_map = {f"{p.device} — {p.description}": p.device for p in list_ports.comports()}
            self.port_box["values"] = tuple(self.port_map)
            self.footer.set("Choose the Atlas CDC port explicitly. ROM DFU is NOT a COM port.")
        except ImportError:
            messagebox.showerror("Dependency missing", "Install requirements.txt into your Python environment; see startup.md.")

    def connect(self) -> None:
        """@brief Open one selected CDC port with DTR; transmit no command."""
        if self.demo or self.serial is not None:
            return
        if self.port.get() not in self.port_map:
            messagebox.showerror("Choose a port", "Refresh ports, then select the Atlas application CDC port.")
            return
        port = None
        try:
            import serial
            # Open with DTR low, discard old OS input, then request a fresh hello.
            port = serial.Serial(port=None, baudrate=115200, timeout=0, write_timeout=0.2,
                                 xonxoff=False, rtscts=False, dsrdtr=False)
            port.dtr, port.rts = False, False
            port.port = self.port_map[self.port.get()]
            port.open()
            port.reset_input_buffer()
            self.session, self.decoder = Session(), Decoder()
            self.confirmed.set(False)
            port.dtr = True
            self.serial = port
            self.event("Opened " + port.port + "; awaiting inhibited bring-up handshake")
        except Exception as exc:
            if port is not None:
                try:
                    port.close()
                except Exception:
                    pass  # Preserve the original open/setup error after cable loss.
            messagebox.showerror("Cannot connect", str(exc))

    def disconnect(self) -> None:
        """@brief Close CDC/DTR; never replay an unfinished operation on reconnect."""
        if self.serial is not None:
            # Release the UI's handle even if the OS reports failure during unplug.
            port, self.serial = self.serial, None
            try:
                port.dtr = False
            except Exception:
                pass  # Cable removal may already have invalidated the handle.
            try:
                port.close()
            except Exception as exc:
                self.event("Serial close failed; connection discarded: " + str(exc))
        if self.session.pending:
            self.event("Disconnected with a pending command: outcome UNKNOWN; no retry sent")
        self.session, self.decoder = Session(), Decoder()
        self.confirmed.set(False)
        self.event("Disconnected; GPIO pulse expires on the MCU, not in this UI")

    def send(self, verb: str) -> None:
        """@brief Send one explicit command after relevant fixture confirmation."""
        if self.demo:
            self.event("SIMULATION ONLY — command not sent: " + verb)
            return
        if self.serial is None:
            messagebox.showwarning("Disconnected", "Open the Atlas application CDC port first.")
            return
        warning = ""
        if verb == "sd test":
            warning = "This writes a NEW 1024-byte ATLASCHK.TST on your inserted test card. Back up the card first. Existing file is refused, never overwritten. Continue?"
        elif verb.startswith("gpio ") and verb != "gpio 0":
            warning = "Only a 3.3 V logic loopback/scope may be attached. This drives the selected logic GPIO HIGH for 1 second. No actuators or loads connected. Continue?"
        elif verb in {"uart", "spi"} or verb.startswith("i2c "):
            warning = "Confirm the exact 3.3 V fixture/pinout in startup.md. SPI shares the IMU bus; never short an active slave output. Perform this explicit transfer?"
        elif verb.startswith("radio ") or verb == "probe radio":
            warning = "DEFERRED TEST: complete the initial acceptance checklist first, power down to attach the correct RFD900x with antenna and verified pinout, then restart. Confirm this later-stage setup is ready?"
        elif verb in {"ble profile", "ble ping"}:
            warning = "This enables BLE advertising/configuration or transmits fixed test text. Use an approved local bench peer and verify the round trip; no NVM save is performed. Continue?"
        if warning and not messagebox.askyesno("Explicit bench action", warning):
            return
        try:
            data = self.session.request(verb, time.monotonic(), self.confirmed.get())
        except ValueError as exc:
            messagebox.showwarning("Request blocked", str(exc))
            return
        self.record({"direction": "tx", "command": data.decode("ascii").strip()})
        try:
            if self.serial.write(data) != len(data):
                raise OSError("Partial command write; outcome uncertain")
            self.event("Sent " + data.decode("ascii").strip())
        except Exception as exc:
            self.session.blocked = "Write outcome UNKNOWN: " + str(exc)
            self.event(self.session.blocked)

    def utc(self) -> None:
        """@brief Set explicitly requested UTC, never on connection or boot."""
        if messagebox.askyesno("RTC time", "Is the laptop clock accurate? Set MCU RTC to current UTC?"):
            t = datetime.now(timezone.utc)
            self.send(f"utc {t.year} {t.month} {t.day} {t.hour} {t.minute} {t.second}")

    def i2c(self) -> None:
        """@brief Read one operator-specified safe register; never scan the bus."""
        self.send(f"i2c {self.address.get()} {self.register.get()}")

    def poll(self) -> None:
        """@brief Nonblocking, bounded receive; redraw at 2 Hz; never send automatically."""
        now = time.monotonic()
        if self.demo and now - self.last_paint >= 0.5:
            import demo as fixture
            self.session.accept(fixture.status(int((now - self.started) * 1000) + 10000), now)
        if self.serial is not None:
            try:
                before = self.decoder.errors
                for frame in self.decoder.feed(self.serial.read(min(self.serial.in_waiting, 8192))):
                    self.session.accept(frame, now)
                    self.record({"direction": "rx", "frame": frame})
                    if frame["type"] == "reply":
                        self.event(f"Reply {frame['id']}: {frame['name']} — {frame['detail']}; verified bytes {frame['verified_bytes']}")
                if self.decoder.errors != before:
                    self.event("Rejected frame: " + self.decoder.last_error)
                    # Unknown/corrupt safety metadata must not leave controls enabled.
                    self.session.blocked = "Invalid telemetry; inspect raw evidence and reconnect manually."
            except Exception as exc:
                self.event("Serial error: " + str(exc))
                self.disconnect()
        self.session.check_timeout(now)
        if now - self.last_paint >= 0.5:
            self.paint(now)
            self.last_paint = now
        self.root.after(20, self.poll)

    def paint(self, now: float) -> None:
        """@brief Show freshness/faults explicitly; old measurements never remain green."""
        s = self.session.status
        fresh = self.session.fresh(now) and not self.session.blocked
        prefix = "SIMULATED — NOT HARDWARE | " if self.demo else ""
        if self.session.blocked:
            state = self.session.blocked
        elif fresh:
            state = "LIVE / PWM & PYRO INHIBITED"
        elif self.serial is not None or self.demo:
            state = "STALE / UNKNOWN"
        else:
            state = "DISCONNECTED / UNKNOWN"
        pending = self.session.pending
        self.banner.set(prefix + state + (f" | waiting for {pending.identifier}: {pending.verb}" if pending else ""))
        if self.session.hello:
            h = self.session.hello
            self.identity.set(f"Firmware {h['version']} · UID " + "-".join(f"{x:08X}" for x in h["uid"]) + f" · CPU {h['clock_hz'] / 1e6:g} MHz")
        else:
            self.identity.set("Awaiting bring-up handshake; Identify requests it again without probing devices")
        if s:
            rows = observations(s)
            for i, values in enumerate(rows):
                name, status, age, detail = values
                if not fresh:
                    status = "STALE / UNKNOWN"
                tag = "bad" if status in ("FAILED", "INVALID DATA") else "good" if status in ("RESPONDING", "FIX REPORTED") else "unknown" if status in ("NOT TESTED", "STALE / UNKNOWN") else "warn"
                iid = str(i)
                if self.table.exists(iid):
                    self.table.item(iid, values=(name, status, age, detail), tags=(tag,))
                else:
                    self.table.insert("", "end", iid=iid, values=(name, status, age, detail), tags=(tag,))
            self.raw.configure(state="normal")
            self.raw.delete("1.0", "end")
            self.raw.insert("1.0", json.dumps(s, indent=2, ensure_ascii=True))
            self.raw.configure(state="disabled")
            stacks = "/".join(str(x) for x in s["tasks"]["stack_words"])
            self.footer.set(f"USB RX/TX drops {s['usb']['rx_drop']}/{s['usb']['tx_drop']} · parser errors {s['tasks']['parser_errors']} · "
                            f"driver fault {s['service']} · free stack words console/owner/IO/SD/USB {stacks} · host rejected {self.decoder.errors}" + (" · LOGGING" if self.log else " · not logging"))
        else:
            for iid in self.table.get_children():
                self.table.delete(iid)
            self.raw.configure(state="normal")
            self.raw.delete("1.0", "end")
            self.raw.configure(state="disabled")

    def select_row(self, _event=None) -> None:
        """@brief Expand a selected row's text, including truncated table content."""
        selection = self.table.selection()
        if selection:
            self.selected.set(" | ".join(str(v) for v in self.table.item(selection[0], "values")))

    def event(self, text: str) -> None:
        """@brief Retain at most 500 visible events. @param text Local diagnostic text."""
        self.event_view.configure(state="normal")
        self.event_view.insert("end", datetime.now().strftime("%H:%M:%S ") + text + "\n")
        self.events += 1
        if self.events > 500:
            self.event_view.delete("1.0", "2.0")
        self.event_view.see("end")
        self.event_view.configure(state="disabled")
        self.record({"event": text})

    def start_log(self) -> None:
        """@brief Create opt-in evidence exclusively; never overwrite a selected file."""
        if self.log:
            return
        if not messagebox.askyesno("Local evidence / privacy", "Logs include device UID, GNSS position, timestamps and test text. Keep private; do not commit them. Create a NEW log?"):
            return
        path = filedialog.asksaveasfilename(defaultextension=".jsonl", initialfile="atlas-" + datetime.now().strftime("%Y%m%d-%H%M%S") + ".jsonl")
        if not path:
            return
        try:
            self.log = Path(path).open("x", encoding="utf-8", newline="\n")
            self.log_bytes = 0
            self.record({"log_schema": 1, "simulated": self.demo, "hello": self.session.hello})
            self.event("Evidence log opened: " + path)
        except OSError as exc:
            messagebox.showerror("Log not opened", str(exc))

    def record(self, item: dict) -> None:
        """@brief Append one bounded local record; stop on disk errors or 100 MiB."""
        if not self.log:
            return
        text = json.dumps(dict(host_utc=datetime.now(timezone.utc).isoformat(), **item), ensure_ascii=True) + "\n"
        try:
            if self.log_bytes + len(text) > 100 * 1024 * 1024:
                raise OSError("100 MiB log limit reached; start a new log explicitly")
            self.log.write(text)
            self.log.flush()
            self.log_bytes += len(text)
        except OSError as exc:
            self.stop_log()
            messagebox.showerror("Logging stopped", str(exc))

    def stop_log(self) -> None:
        """@brief Close the log without deleting or replacing it."""
        if self.log:
            file, self.log = self.log, None
            try:
                file.close()
            except OSError:
                pass

    def add_note(self) -> None:
        """@brief Record DMM/scope/peer evidence entered by the operator."""
        text = self.note.get().strip()
        if text:
            self.event("OPERATOR NOTE: " + text[:1000])
            self.note.set("")

    def close(self) -> None:
        """@brief Close resources; no target reset, erase or implicit power command."""
        self.disconnect()
        self.stop_log()
        self.root.destroy()


def main() -> None:
    """@brief Launch a disconnected dashboard or inert demo/smoke test."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--demo", action="store_true", help="Synthetic UI only; serial disabled")
    parser.add_argument("--smoke-test", action="store_true", help="Build/update hidden demo UI, then exit; no COM access")
    args = parser.parse_args()
    root = tk.Tk()
    if args.smoke_test:
        root.withdraw()
    app = Dashboard(root, args.demo or args.smoke_test)
    if args.smoke_test:
        import demo
        app.session.accept(demo.status(), time.monotonic())
        app.paint(time.monotonic())
        root.update_idletasks()
        assert len(app.table.get_children()) == 14
        assert app.serial is None
        print("Dashboard hidden smoke test: 14 observation rows; no serial access")
        app.close()
    else:
        root.mainloop()


if __name__ == "__main__":
    main()
