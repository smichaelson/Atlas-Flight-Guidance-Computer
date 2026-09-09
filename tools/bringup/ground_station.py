"""Atlas Ground Station: offline browser instruments with one local USB owner.

Major functions: Station serializes hardware work and validates every frame;
Handler serves a fixed local UI and token/origin-protected API; main binds only
127.0.0.1. Demo data never opens serial. Opening the UI never starts a board test.
"""
from __future__ import annotations
import argparse
from collections import deque
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import math
from pathlib import Path
import secrets
import threading
import time
import webbrowser

import demo
from protocol import Decoder, MODULES, Session, observations, validate
import update_firmware as updater

WEB = Path(__file__).parent / "web"


def simulated(ms: int) -> dict:
    """Produce labelled moving instrument fixtures, never hardware evidence."""
    s = demo.status(ms)
    t = ms / 1000
    roll, pitch, yaw = .22 * math.sin(t / 4), .12 * math.cos(t / 5), .5 + t / 90
    cr, sr, cp, sp, cy, sy = (math.cos(roll / 2), math.sin(roll / 2),
                             math.cos(pitch / 2), math.sin(pitch / 2),
                             math.cos(yaw / 2), math.sin(yaw / 2))
    s["bno"]["q_ppm"] = [round(v * 1e6) for v in
                          (cr*cp*cy+sr*sp*sy, sr*cp*cy-cr*sp*sy,
                           cr*sp*cy+sr*cp*sy, cr*cp*sy-sr*sp*cy)]
    s["bno"]["accuracy"] = [3] * 4
    s["lsm"]["mg"] = [round(80*math.sin(t)), round(55*math.cos(t*.7)), round(1000+12*math.sin(t*2))]
    s["lsm"]["mdps"] = [round(2800*math.cos(t)), round(1700*math.sin(t)), 320]
    s["adxl"]["mg"] = list(s["lsm"]["mg"])
    s["baro"]["pa"] = round(100885 + 18*math.sin(t/6))
    s["power"]["mv"][3] = round(15120+15*math.sin(t/8))
    s["gnss"].update(fix=3, flags=1, sv=17, hacc_mm=850, pps_count=ms//1000,
                     pps_us=1000000, lat_e7=round(341020000+300*math.sin(t/15)),
                     lon_e7=round(-1177110000+300*math.cos(t/15)), h_msl_mm=round(354000+300*math.sin(t/5)))
    s["attempted"] = 255
    s["init"][9] = 0
    s["radio"]["rx"] = ms//10
    s["radio"]["last_hex"] = "41544c41535f44454d4f"
    return s


class Station:
    """Bounded state, one serial owner, no arbitrary command terminal or RF retries."""
    def __init__(self, manifest: Path = updater.DEFAULT_MANIFEST):
        self.lock = threading.RLock()
        self.serial = None
        self.port = ""
        self.mode = "disconnected"
        self.session = Session()
        self.decoder = Decoder()
        self.history = deque(maxlen=240)
        self.events = deque(maxlen=160)
        self.records = deque(maxlen=12000)
        self.recording = False
        self.record_full = False
        self.sequence = 0
        self.generation = 0
        self.started = time.monotonic()
        self.last_demo = 0.0
        self.batch: list[str] = []
        self.confirmed = False
        self.manifest = manifest
        self.frozen = None
        self.firmware = dict(state="unchecked", message="Check the build before updating.")
        self.updating = False
        self.stop = threading.Event()

    def event(self, message: str, kind="info"):
        self.sequence += 1
        self.events.append(dict(id=self.sequence, time=datetime.now().strftime("%H:%M:%S"),
                                kind=kind, message=str(message)[:5000]))

    def disconnect(self):
        if self.serial:
            self.serial.close()
        self.serial = None
        self.port = ""
        self.mode = "disconnected"
        self.batch.clear()
        self.confirmed = False
        self.recording = False
        self.generation += 1
        self.session, self.decoder = Session(), Decoder()
        self.history.clear()

    def ports(self):
        from serial.tools import list_ports
        return [dict(device=p.device, description=p.description, vid=p.vid, pid=p.pid)
                for p in list_ports.comports()]

    def ingest(self, frame: dict, now: float):
        self.session.accept(frame, now)
        if frame["type"] == "status":
            self.history.append(frame)
        elif frame["type"] == "reply":
            self.event(f"#{frame['id']} · {frame['name']} · {frame['detail']}",
                       "error" if frame["status"] else "success")
        elif frame["type"] == "hello":
            self.event("Identified " + frame["version"], "success")
        if self.recording:
            if len(self.records) == self.records.maxlen:
                self.recording = False
                self.record_full = True
                self.event("Recording reached its limit. Export this capture before starting another.", "error")
            else:
                self.records.append(dict(host_utc=datetime.now(timezone.utc).isoformat(),
                                         source=self.mode, frame=frame))

    def send(self, verb: str):
        if self.mode != "live" or not self.serial or self.updating:
            raise ValueError("Connect to Atlas to run a hardware test. Demo controls never send commands.")
        packet = self.session.request(verb, time.monotonic(), self.confirmed)
        try:
            if self.serial.write(packet) != len(packet):
                raise IOError("Short serial write")
        except Exception:
            self.session.blocked = "Serial write failed; outcome unknown. Inspect before reconnecting."
            self.batch.clear()
            raise
        self.event("Sent · " + verb)

    def tick(self):
        with self.lock:
            now = time.monotonic()
            if self.mode == "demo" and now - self.last_demo >= .5:
                self.last_demo = now
                self.ingest(simulated(int((now-self.started)*1000)+10000), now)
            if self.serial and not self.updating:
                try:
                    data = self.serial.read(min(self.serial.in_waiting, 32768))
                    previous_errors = self.decoder.errors
                    for frame in self.decoder.feed(data):
                        self.ingest(frame, now)
                    if self.decoder.errors != previous_errors:
                        self.session.blocked = "Invalid telemetry received. Inspect the capture and reconnect."
                        self.event(self.decoder.last_error, "error")
                        self.batch.clear()
                    self.session.check_timeout(now)
                    if self.batch and not self.session.pending and not self.session.blocked:
                        if self.session.fresh(now) and not self.session.status["pending_id"]:
                            self.send(self.batch.pop(0))
                except (OSError, ValueError) as exc:
                    self.event("USB connection lost: " + str(exc), "error")
                    self.disconnect()

    def run(self):
        while not self.stop.wait(.025):
            self.tick()

    def snapshot(self):
        with self.lock:
            now = time.monotonic()
            fresh = self.session.fresh(now)
            age = now-self.session.received_at
            return dict(mode=self.mode, port=self.port, generation=self.generation,
                        fresh=fresh, age_ms=round(age*1000) if math.isfinite(age) else None,
                        hello=self.session.hello, status=self.session.status,
                        rows=observations(self.session.status) if self.session.status else [],
                        history=list(self.history), events=list(self.events),
                        blocked=self.session.blocked, pending=self.session.pending.verb if self.session.pending else None,
                        batch=list(self.batch), confirmed=self.confirmed,
                        decoder_errors=self.decoder.errors, recording=self.recording,
                        record_count=len(self.records), record_full=self.record_full,
                        firmware=self.firmware, updating=self.updating)

    def update_worker(self, port, uid, frozen):
        def report(message):
            with self.lock:
                self.firmware = dict(state="updating", message=message)
                self.event(message)
        try:
            updater.enter_dfu(port, uid, report)
            result = updater.program(frozen, uid, report=report)
            with self.lock:
                message = ("Flash verified. Cycle battery power with BOOT0 low, then reconnect." if result["power_cycle_required"]
                           else "Flash verified. Reconnect to confirm the new firmware is running.")
                self.firmware = dict(state="verified", message=message, result=result)
                self.event(self.firmware["message"], "success")
        except Exception as exc:
            with self.lock:
                self.firmware = dict(state="failed", message=str(exc))
                self.event("Update stopped: " + str(exc), "error")
        finally:
            with self.lock:
                self.updating = False

    def action(self, action: str, body: dict):
        with self.lock:
            if self.updating:
                raise ValueError("An update is in progress. Keep battery power and USB connected.")
            if action == "connect":
                port = body.get("port")
                if port not in [p["device"] for p in self.ports()]:
                    raise ValueError("Select a currently enumerated Atlas COM port.")
                if body.get("confirmed") is not True:
                    raise ValueError("Confirm the battery, USB and disconnected-load setup first.")
                if self.mode != "disconnected":
                    raise ValueError("Disconnect or exit demo before connecting a device.")
                import serial
                device = serial.Serial(port=None, baudrate=115200, timeout=0, write_timeout=1)
                device.dtr, device.rts = True, False
                device.port = port
                try:
                    device.open()
                except Exception:
                    device.close()
                    raise
                self.serial, self.port, self.mode, self.confirmed = device, port, "live", True
                self.generation += 1
                self.event("Connected to " + port + ". Waiting for inhibited firmware handshake.")
            elif action == "disconnect":
                self.disconnect()
                self.event("Disconnected. Measurements cleared.")
            elif action == "demo":
                if self.mode != "disconnected":
                    raise ValueError("Disconnect before entering demo.")
                self.disconnect()
                self.mode, self.started = "demo", time.monotonic()
                self.ingest(demo.hello(), time.monotonic())
                self.ingest(simulated(10000), time.monotonic())
                self.event("DEMO · All displayed measurements are simulated.")
            elif action == "command":
                verb = body.get("verb", "")
                if not isinstance(verb, str) or len(verb) > 90 or verb.startswith("bootloader"):
                    raise ValueError("Use the verified firmware-update workflow.")
                if self.batch:
                    raise ValueError("Wait for the sensor sequence to finish.")
                # Fixture/RF/media mutations carry an additional deliberate UI acknowledgement.
                if verb not in {"hello", "status", "stop", "sd mount", "sd read", "sd unmount"} and not verb.startswith("probe "):
                    if body.get("action_confirmed") is not True:
                        raise ValueError("Review and confirm this specific bench test.")
                self.send(verb)
            elif action == "sensors":
                if self.mode != "live" or not self.session.fresh(time.monotonic()) or not self.confirmed:
                    raise ValueError("Need a confirmed connection and fresh telemetry.")
                if self.session.pending or self.batch or self.session.blocked:
                    raise ValueError("An operation is pending or the session needs attention.")
                mask = self.session.status["attempted"]
                self.batch = [f"probe {name}" for i, name in enumerate(MODULES[:6]) if not mask & (1 << i)]
                self.event("Requested onboard sensor sequence; each untested module is probed once.")
            elif action == "record":
                if body.get("enabled") is True:
                    if not self.session.hello:
                        raise ValueError("Connect or start demo before recording.")
                    if self.records:
                        raise ValueError("Export and clear the previous capture before recording another.")
                    self.recording, self.record_full = True, False
                    self.ingest(self.session.hello, time.monotonic())
                else:
                    self.recording = False
            elif action == "clear-record":
                if self.recording:
                    raise ValueError("Stop recording first.")
                self.records.clear()
                self.record_full = False
            elif action == "firmware-check":
                raw = body.get("manifest", str(self.manifest))
                if not isinstance(raw, str) or len(raw) > 1024:
                    raise ValueError("Invalid manifest path.")
                evidence, self.frozen = updater.prepare(Path(raw))
                self.firmware = dict(state="checked", message="Image hashes, target and flash addresses verified.",
                                     evidence=evidence, key=secrets.token_urlsafe(24))
            elif action == "firmware-update":
                if self.mode != "live" or not self.serial or body.get("action_confirmed") is not True:
                    raise ValueError("Connect to Atlas and confirm the reviewed firmware update.")
                if self.firmware.get("state") != "checked" or body.get("key") != self.firmware.get("key"):
                    raise ValueError("Check this firmware image before updating.")
                if self.session.pending or self.batch or self.session.blocked or not self.session.fresh(time.monotonic()):
                    raise ValueError("Wait for idle, fresh telemetry before updating.")
                uid = self.session.hello["uid"]
                # Apply every client gate before surrendering the current USB owner.
                self.session.request("bootloader " + " ".join(str(w) for w in uid), time.monotonic(), self.confirmed)
                self.session.pending = None  # Dry preflight; the new owner sends exactly once.
                updater.programmer_path()
                port, frozen = self.port, self.frozen
                self.disconnect()
                self.updating = True
                self.firmware = dict(state="updating", message="Preparing the USB bootloader transition…")
                threading.Thread(target=self.update_worker, args=(port, uid, frozen), daemon=True).start()
            else:
                raise ValueError("Unknown action.")
            return {"ok": True}


class Server(ThreadingHTTPServer):
    daemon_threads = True
    def __init__(self, address, station):
        self.station = station
        self.token = secrets.token_urlsafe(32)
        super().__init__(address, Handler)
        self.origin = f"http://127.0.0.1:{self.server_port}"


class Handler(BaseHTTPRequestHandler):
    """No CORS, fixed assets, host validation, bounded JSON, session token on APIs."""
    def log_message(self, *_):
        pass

    def reply(self, data, status=200, content_type="application/json"):
        if isinstance(data, (dict, list)):
            data = json.dumps(data, ensure_ascii=True, allow_nan=False).encode()
        if isinstance(data, str):
            data = data.encode()
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("X-Frame-Options", "DENY")
        self.send_header("Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'")
        self.end_headers()
        self.wfile.write(data)

    def authorized(self, mutation=False):
        if self.headers.get("Host") != self.server.origin.removeprefix("http://"):
            self.reply({"error": "Invalid host"}, 403)
            return False
        if self.headers.get("Sec-Fetch-Site") not in (None, "same-origin", "none"):
            self.reply({"error": "Cross-origin access denied"}, 403)
            return False
        if self.path.startswith("/api/"):
            token = self.headers.get("X-Atlas-Token", "")
            if not secrets.compare_digest(token, self.server.token):
                self.reply({"error": "Missing session token"}, 403)
                return False
            if mutation and self.headers.get("Origin") not in (None, self.server.origin):
                self.reply({"error": "Invalid origin"}, 403)
                return False
        return True

    def do_GET(self):
        if not self.authorized():
            return
        if self.path == "/":
            self.reply((WEB / "index.html").read_text(encoding="utf-8").replace("__ATLAS_TOKEN__", self.server.token), content_type="text/html; charset=utf-8")
        elif self.path in ("/app.js", "/style.css"):
            kind = "text/javascript" if self.path.endswith(".js") else "text/css"
            self.reply((WEB / self.path[1:]).read_bytes(), content_type=kind + "; charset=utf-8")
        elif self.path == "/api/state":
            self.reply(self.server.station.snapshot())
        elif self.path == "/api/ports":
            try:
                self.reply(self.server.station.ports())
            except (ImportError, OSError) as exc:
                self.reply({"error": str(exc)}, 400)
        elif self.path == "/api/capture":
            with self.server.station.lock:
                lines = [json.dumps(record) for record in self.server.station.records]
            self.reply("\n".join(lines) + "\n", content_type="application/x-ndjson")
        else:
            self.reply({"error": "Not found"}, 404)

    def do_POST(self):
        if not self.authorized(True):
            return
        try:
            if self.headers.get("Content-Type") != "application/json":
                raise ValueError("JSON required.")
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= 4096:
                raise ValueError("Invalid request size.")
            self.connection.settimeout(3)
            body = json.loads(self.rfile.read(length))
            if not isinstance(body, dict):
                raise ValueError("Expected an object.")
            self.reply(self.server.station.action(self.path.removeprefix("/api/"), body))
        except (ValueError, OSError, ImportError) as exc:
            self.reply({"error": str(exc)}, 400)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--demo", action="store_true")
    parser.add_argument("--open", action="store_true")
    parser.add_argument("--manifest", type=Path, default=updater.DEFAULT_MANIFEST)
    args = parser.parse_args()
    station = Station(args.manifest)
    if args.demo:
        station.action("demo", {})
    server = Server(("127.0.0.1", args.port), station)
    threading.Thread(target=station.run, daemon=True).start()
    print("Atlas Ground Station · " + server.origin, flush=True)
    if args.open:
        webbrowser.open(server.origin)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        station.stop.set()
        with station.lock:
            station.disconnect()
        server.server_close()


if __name__ == "__main__":
    main()
