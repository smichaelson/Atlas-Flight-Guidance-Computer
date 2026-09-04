"""Atlas bench protocol and observation model (no serial or hardware access).

Major functions: Decoder.feed bounds and validates JSON frames; Session.request
fences commands after stale data, reset or uncertain completion; observations
labels transport evidence without claiming physical qualification.
"""
from __future__ import annotations

from dataclasses import dataclass
import json
import re

MAX_LINE = 8192
STALE_SECONDS = 2.5
COMMAND_SECONDS = 45.0
STATUS_NAMES = (
    "OK", "NULL", "ARGUMENT", "BUSY", "NOT_READY", "TIMEOUT", "IO",
    "IDENTITY", "CRC", "PROTOCOL", "NACK", "OVERFLOW", "UNSUPPORTED", "STATE",
)
MODULES = ("adxl", "lsm", "mmc", "baro", "bno", "gnss", "ble", "radio")
GNSS_FAILURE_STAGES = (
    "none", "transport init", "transport start", "MON-VER write",
    "MON-VER service", "MON-VER timeout", "timebase start",
    "PPS capture start", "configuration write", "configuration readback",
    "runtime service",
)
IO_REFERENCE_FAILURE_STAGES = (
    "none", "ADC3 channel configuration", "ADC3 conversion start",
    "ADC3 overrun", "ADC3 deadline", "ADC3 conversion poll",
    "ADC3 conversion stop", "ADC3 raw range", "computed VDDA range",
    "computed die-temperature range",
)


def _integer(value: object, low: int = 0, high: int = 0xFFFFFFFF) -> bool:
    """@brief Reject booleans, floats and out-of-range integers. @return Validity."""
    return type(value) is int and low <= value <= high


def _array(value: object, size: int, *, signed: bool = False, nullable: bool = False) -> bool:
    """@brief Check the exact shape of a wire array. @return Validity."""
    return (isinstance(value, list) and len(value) == size and
            all((nullable and v is None) or _integer(v, -0x80000000 if signed else 0,
                                                    0x7FFFFFFF if signed else 0xFFFFFFFF)
                for v in value))


def _object(pairs: list[tuple[str, object]]) -> dict:
    """@brief Reject ambiguous duplicate JSON keys. @return Decoded object."""
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("Duplicate JSON key")
        result[key] = value
    return result


def _constant(value: str) -> None:
    """@brief Reject JSON extensions NaN/Infinity. @param value Invalid token."""
    raise ValueError(f"Non-JSON numeric constant: {value}")


def validate(frame: object) -> dict:
    """@brief Validate every field consumed by the UI. @return Valid frame or raise.

    Extra schema-1 diagnostic fields are retained for evidence, not executed.
    """
    if not isinstance(frame, dict):
        raise ValueError("Expected an object")
    kind = frame.get("type")
    if kind == "hello":
        if (frame.get("profile") != "bringup" or not _integer(frame.get("schema"), 1, 1) or
                frame.get("pwm_pyro_inhibited") is not True or
                frame.get("led_inhibited") is not True or
                not isinstance(frame.get("version"), str) or len(frame["version"]) > 32 or
                not _array(frame.get("uid"), 3) or not _integer(frame.get("device_id")) or
                not _integer(frame.get("clock_hz"), 1)):
            raise ValueError("Not a recognized inhibited Atlas bring-up image")
    elif kind == "reply":
        if (not _integer(frame.get("id"), 1) or not _integer(frame.get("status"), 0, 13) or
                not _integer(frame.get("verified_bytes")) or
                any(not isinstance(frame.get(k), str) or len(frame[k]) > 192
                    for k in ("name", "detail"))):
            raise ValueError("Malformed command completion")
    elif kind == "error":
        if not isinstance(frame.get("reason"), str) or len(frame["reason"]) > 192:
            raise ValueError("Malformed target error")
    elif kind == "status":
        if (frame.get("profile") != "bringup" or not _integer(frame.get("schema"), 1, 1) or
                frame.get("inhibited") is not True):
            raise ValueError("Wrong status profile or missing inhibit")
        for key in ("seq", "ms", "owner_ms", "attempted", "pending_id", "service"):
            if not _integer(frame.get(key)):
                raise ValueError(f"Invalid {key}")
        for key, size in (("init", 12), ("count", 4), ("errors", 4), ("sample_status", 4)):
            if not _array(frame.get(key), size):
                raise ValueError(f"Invalid {key} array")
        required = {
            "adxl": ("t",), "lsm": ("t", "irq"), "mmc": ("t",), "baro": ("t",),
            "gnss": ("t", "frames", "crc_errors", "timeouts", "fix", "flags", "sv",
                     "hacc_mm", "tow_ms", "pps_count", "pps_us", "failure_stage",
                     "failure_status", "pps_started", "rx_bytes", "tx_bytes", "dropped",
                     "uart_errors", "restarts", "preflights", "start_retries",
                     "hal_status", "hal_error"),
            "power": ("start", "status", "t", "count", "valid", "vdda_mv", "adc_errors",
                      "ref_stage", "ref_channel", "ref_raw", "ref_hal_status",
                      "ref_hal_error", "reset_flags", "power_events", "ecc_events"),
            "gpio": ("inputs", "outputs", "switch", "pwm", "armed"),
            "sd": ("start", "card", "mounted", "status", "fs", "completed", "errors", "time_valid"),
            "ble": ("command", "dtr", "rx", "tx", "timeouts"), "radio": ("rx", "command"),
            "usb": ("session", "rx", "rx_drop", "tx", "tx_drop", "timeouts"),
            "tasks": ("console", "owner", "watchdog", "fault", "busy", "parser_errors", "response_drops"),
        }
        for section, keys in required.items():
            part = frame.get(section)
            if not isinstance(part, dict) or any(not _integer(part.get(k)) for k in keys):
                raise ValueError(f"Invalid {section} object")
        bno = frame.get("bno")
        if not isinstance(bno, dict):
            raise ValueError("Missing BNO reports")
        bno_health = bno.get("health")
        if bno_health is not None:
            counter_keys = ("interrupts", "reads", "writes", "io_errors", "protocol_errors",
                            "decoded", "decode_errors", "resets", "recovery_attempts",
                            "recovery_failures", "last_hal_error")
            if (not isinstance(bno_health, dict) or
                    any(not _integer(bno_health.get(k)) for k in counter_keys) or
                    not _integer(bno_health.get("last_hal_status"), 0, 3) or
                    not _integer(bno_health.get("failure_stage"), 0, 9) or
                    not _integer(bno_health.get("last_length"), 0, 1024) or
                    not _integer(bno_health.get("pending_length"), 0, 1024) or
                    not _integer(bno_health.get("intn_low"), 0, 1) or
                    not _integer(bno_health.get("initialized"), 0, 1) or
                    bno_health.get("recovery_failures", 1) >
                    bno_health.get("recovery_attempts", 0)):
                raise ValueError("Invalid bno.health object")
        led = frame.get("led")
        if (not isinstance(led, dict) or
                not _integer(led.get("commanded"), 0, 0) or
                not _integer(led.get("gates"), 0, 7) or
                not _integer(led.get("initialized"), 0, 1) or
                not _integer(led.get("inhibited"), 1, 1)):
            raise ValueError("Invalid or uninhibited led object")
        if (not _integer(frame["gnss"].get("failure_stage"), 0,
                         len(GNSS_FAILURE_STAGES) - 1) or
                not _integer(frame["gnss"].get("failure_status"), 0, 13) or
                not _integer(frame["gnss"].get("pps_started"), 0, 1) or
                not _integer(frame["gnss"].get("hal_status"), 0, 3)):
            raise ValueError("Invalid GNSS diagnostic state")
        if (not _integer(frame["power"].get("ref_stage"), 0,
                         len(IO_REFERENCE_FAILURE_STAGES) - 1) or
                not _integer(frame["power"].get("ref_channel"), 0, 1) or
                not _integer(frame["power"].get("ref_hal_status"), 0, 3)):
            raise ValueError("Invalid ADC3 reference diagnostic state")
        arrays = (("adxl", "mg", 3, True), ("lsm", "mg", 3, True),
                  ("lsm", "mdps", 3, True), ("mmc", "nt", 3, True),
                  ("bno", "count", 4, False), ("bno", "t", 4, False),
                  ("bno", "accuracy", 4, False), ("bno", "accel_mm_s2", 3, True),
                  ("bno", "gyro_mrad_s", 3, True), ("bno", "mag_nt", 3, True),
                  ("bno", "q_ppm", 4, True), ("power", "mv", 10, False),
                  ("power", "raw", 10, False), ("sd", "utc", 6, False),
                  ("tasks", "stack_words", 5, False))
        for section, key, size, signed in arrays:
            if not _array(frame[section].get(key), size, signed=signed, nullable=signed):
                raise ValueError(f"Invalid {section}.{key}")
        for section, key in (("lsm", "temp_cc"), ("baro", "temp_cc"), ("baro", "pa"),
                             ("power", "temp_c"), ("gnss", "lat_e7"),
                             ("gnss", "lon_e7"), ("gnss", "h_msl_mm")):
            value = frame[section].get(key)
            if key not in frame[section] or (value is not None and not _integer(value, -0x80000000, 0x7FFFFFFF)):
                raise ValueError(f"Invalid {section}.{key}")
        for section, key in (("gnss", "version"), ("ble", "model"), ("ble", "firmware"),
                             ("ble", "last_hex"), ("radio", "last_hex")):
            if not isinstance(frame[section].get(key), str) or len(frame[section][key]) > 192:
                raise ValueError(f"Invalid {section}.{key}")
        if type(frame["power"].get("available")) is not bool:
            raise ValueError("Missing analog availability")
        if frame["gpio"]["pwm"] or frame["gpio"]["armed"]:
            raise ValueError("Unexpected enabled PWM/armed pyro in bring-up image")
    else:
        raise ValueError("Unknown message type")
    return frame


class Decoder:
    """Bounded LF framer. Corrupt/overlong input cannot become a valid suffix."""

    def __init__(self) -> None:
        """@brief Initialize an empty, unsynchronized stream."""
        self.buffer = bytearray()
        self.discard = False
        self.errors = 0
        self.last_error = ""

    def feed(self, data: bytes) -> list[dict]:
        """@brief Accept arbitrary fragments. @param data Bytes. @return Valid records."""
        frames = []
        for byte in data:
            if byte == 10:
                if self.discard:
                    self.errors += 1
                    self.last_error = "Overlong record discarded"
                elif self.buffer.strip():
                    try:
                        frames.append(validate(json.loads(self.buffer.decode("ascii"),
                                                          object_pairs_hook=_object,
                                                          parse_constant=_constant)))
                    except (ValueError, RecursionError) as exc:
                        self.errors += 1
                        self.last_error = str(exc)
                self.buffer.clear()
                self.discard = False
            elif not self.discard:
                if len(self.buffer) >= MAX_LINE:
                    self.buffer.clear()
                    self.discard = True
                else:
                    self.buffer.append(byte)
        return frames


def valid_command(verb: str) -> bool:
    """@brief Match the firmware allowlist, never arbitrary terminal text. @return Validity."""
    if verb in {"hello", "status", "beep", "stop", "uart", "spi", "sd mount", "sd read",
                "sd test", "sd unmount", "ble profile", "ble data", "ble command", "ble ping",
                "radio id", "radio ping"}:
        return True
    if verb in {f"probe {module}" for module in MODULES}:
        return True
    if verb == "led 0" or re.fullmatch(r"gpio [0-7]", verb):
        return True
    if re.fullmatch(r"i2c [0-9]{1,3} [0-9]{1,3}", verb):
        _, address, register = verb.split()
        return 8 <= int(address) <= 119 and int(register) <= 255
    if re.fullmatch(r"utc [0-9]{4}(?: [0-9]{1,2}){5}", verb):
        from datetime import datetime
        try:
            parts = [int(v) for v in verb.split()[1:]]
            datetime(*parts)
            return 2000 <= parts[0] <= 2099
        except ValueError:
            return False
    return False


@dataclass
class Pending:
    """One command whose completion has not yet been observed."""
    identifier: int
    verb: str
    began: float


class Session:
    """Fail-closed client state; no implicit reconnect, writes or retry."""

    def __init__(self) -> None:
        """@brief Create a new manually opened connection generation."""
        self.hello: dict | None = None
        self.status: dict | None = None
        self.received_at = float("-inf")
        self.next_id = 1
        self.pending: Pending | None = None
        self.blocked = ""
        self.last_reply: dict | None = None

    def accept(self, frame: dict, now: float) -> None:
        """@brief Apply a validated frame. @param now Monotonic laptop seconds."""
        validate(frame)
        if frame["type"] == "hello":
            if self.hello and self.hello["uid"] != frame["uid"]:
                self.blocked = "Device identity changed; disconnect and inspect."
            else:
                self.hello = frame
        elif frame["type"] == "status" and self.hello:
            if self.status and (frame["usb"]["session"] != self.status["usb"]["session"] or
                                ((frame["ms"] - self.status["ms"]) & 0xFFFFFFFF) >= 0x80000000):
                self.blocked = "MCU/session restarted; command outcome may be unknown. Reconnect manually."
            self.status = frame
            self.received_at = now
            if frame["tasks"]["fault"]:
                self.blocked = "Firmware supervisor fault; capture evidence and power down."
        elif frame["type"] == "reply":
            if self.pending and frame["id"] == self.pending.identifier:
                self.last_reply = dict(frame, command=self.pending.verb)
                self.pending = None
        elif frame["type"] == "error":
            self.blocked = "Target error: " + frame["reason"]

    def fresh(self, now: float) -> bool:
        """@brief Require recent status, not merely a connected COM port. @return Freshness."""
        return self.status is not None and 0 <= now - self.received_at <= STALE_SECONDS

    def check_timeout(self, now: float) -> None:
        """@brief Latch uncertainty without retrying a potentially completed action."""
        if self.pending and now - self.pending.began > COMMAND_SECONDS:
            self.blocked = "Command timed out; outcome UNKNOWN. Do not repeat blindly; inspect/reconnect."

    def request(self, verb: str, now: float, confirmed: bool) -> bytes:
        """@brief Prepare exactly one command. @return ASCII line; caller sends once.

        @param verb Allowlisted operation. @param now Monotonic seconds.
        @param confirmed Operator has completed physical power/load checks.
        """
        self.check_timeout(now)
        if not valid_command(verb):
            raise ValueError("Command is not in the diagnostic allowlist")
        if self.blocked or self.pending:
            raise ValueError(self.blocked or "Wait for the current command; no queued/repeated actions")
        if verb not in {"hello", "status"}:
            if not confirmed or not self.hello or not self.fresh(now):
                raise ValueError("Need power/load checklist, bring-up handshake and fresh telemetry")
            if self.status["pending_id"]:
                raise ValueError("MCU still has an outstanding operation; wait for it to finish")
        if self.next_id > 0xFFFFFFFF:
            raise ValueError("Command IDs exhausted; reconnect manually")
        self.pending = Pending(self.next_id, verb, now)
        self.next_id += 1
        return f"{self.pending.identifier} {verb}\n".encode("ascii")


def age_ms(now: int, stamp: int) -> int:
    """@brief Compute modulo-32-bit MCU age. @return Milliseconds since stamp."""
    return (now - stamp) & 0xFFFFFFFF


def _values(values: list, divisor: float, unit: str) -> str:
    """@brief Format scaled integers without concealing nulls. @return Display text."""
    return ", ".join("invalid" if v is None else f"{v / divisor:.3f}" for v in values) + " " + unit


def observations(s: dict) -> list[tuple[str, str, str, str]]:
    """@brief Build evidence rows from one validated fresh status. @return Name/state/age/detail.

    These labels describe communication and data freshness, NOT calibration,
    connector correctness, RF range, motor/pyro qualification or flight readiness.
    """
    rows = []
    for index, (key, label) in enumerate(zip(MODULES[:4], ("ADXL375", "LSM6DSV16B", "MMC5983MA", "MS5611"))):
        age = age_ms(s["ms"], s[key]["t"])
        state = ("NOT TESTED" if not s["attempted"] & (1 << index) else
                 "FAILED" if s["init"][index] or s["sample_status"][index] not in (0, 4) else
                 "NO SAMPLES" if not s["count"][index] else "STALE" if age > 1500 else "RESPONDING")
        if key in ("adxl", "lsm"):
            detail = _values(s[key]["mg"], 1000, "g")
            if key == "lsm":
                detail += "; gyro " + _values(s[key]["mdps"], 1000, "deg/s")
        elif key == "mmc":
            detail = _values(s[key]["nt"], 1000, "uT")
        else:
            detail = f"{s[key]['pa']} Pa; {s[key]['temp_cc'] / 100:.2f} C" if s[key]["temp_cc"] is not None else "invalid temperature"
        vectors = (s[key]["mg"] + s[key].get("mdps", []) if key in ("adxl", "lsm") else
                   s[key]["nt"] if key == "mmc" else [s[key]["pa"], s[key]["temp_cc"]])
        if state == "RESPONDING" and any(v is None for v in vectors):
            state = "INVALID DATA"
        if state in ("NOT TESTED", "NO SAMPLES"):
            detail = "No accepted measurement yet"
        detail += f"; samples {s['count'][index]}, errors {s['errors'][index]}, init {s['init'][index]}"
        rows.append((label, state, str(age) if s["count"][index] else "--", detail))
    bno = s["bno"]
    for i, name in enumerate(("BNO accel", "BNO gyro", "BNO magnetic", "BNO quaternion")):
        age = age_ms(s["ms"], bno["t"][i])
        state = ("NOT TESTED" if not s["attempted"] & 16 else "FAILED" if any(s["init"][4:6]) else
                 "NO SAMPLES" if bno["count"][i] == 0 else "STALE" if age > 1500 else "RESPONDING")
        key, scale, unit = (("accel_mm_s2", 1000, "m/s2"), ("gyro_mrad_s", 1000, "rad/s"),
                            ("mag_nt", 1000, "uT"), ("q_ppm", 1e6, "[w,x,y,z]"))[i]
        if state == "RESPONDING" and any(v is None for v in bno[key]):
            state = "INVALID DATA"
        detail = _values(bno[key], scale, unit) + \
                 f"; count {bno['count'][i]}, accuracy {bno['accuracy'][i]}"
        if i == 0 and isinstance(bno.get("health"), dict):
            h = bno["health"]
            detail += (f"; I2C reads/writes {h['reads']}/{h['writes']}, errors {h['io_errors']}, "
                       f"stage {h['failure_stage']}, HAL {h['last_hal_status']}/0x{h['last_hal_error']:08X}, "
                       f"pending {h['pending_length']}, INTN {h['intn_low']}")
        rows.append((name, state, str(age) if bno["count"][i] else "--", detail))
    g = s["gnss"]
    age = age_ms(s["ms"], g["t"])
    state = ("NOT TESTED" if not s["attempted"] & 32 else "FAILED" if any(s["init"][6:8]) else
             "NO FRAMES" if not g["frames"] else "STALE" if age > 2500 else
             "FIX REPORTED" if g["flags"] & 1 and g["fix"] in (3, 4) else "RESPONDING / NO 3D FIX")
    rows.append(("GNSS + PPS", state, str(age) if g["frames"] else "--",
                 f"{g['sv']} satellites; fix {g['fix']}; NAV {g['frames']}; CRC {g['crc_errors']}; "
                 f"PPS {g['pps_count']} / {g['pps_us']} us (started {g['pps_started']}); "
                 f"stage {g['failure_stage']} {GNSS_FAILURE_STAGES[g['failure_stage']]} / status {g['failure_status']}; "
                 f"UART RX/TX {g['rx_bytes']}/{g['tx_bytes']}, errors {g['uart_errors']}, "
                 f"preflight/retry {g['preflights']}/{g['start_retries']}, "
                 f"HAL {g['hal_status']}/0x{g['hal_error']:08X}; {g['version']}"))
    for module, init_index, bit in (("ble", 8, 64), ("radio", 9, 128)):
        part = s[module]
        state = "NOT TESTED" if not s["attempted"] & bit else "FAILED" if s["init"][init_index] else "TRANSPORT INITIALIZED"
        detail = f"command mode {part['command']}; RX bytes {part['rx']}; last hex {part['last_hex']}"
        if module == "ble":
            detail = f"{part['model']} {part['firmware']}; " + detail
        rows.append(("BLE" if module == "ble" else "RFD900x (optional)", state, "--", detail + "; verify peer round trip"))
    sd = s["sd"]
    state = "NO CARD" if not sd["card"] else "MOUNTED" if sd["mounted"] else "NOT MOUNTED"
    rows.append(("SD card", state, "--", f"last status {sd['status']}, FatFs {sd['fs']}; "
                 f"completed {sd['completed']}, errors {sd['errors']}; read/write tests require explicit commands"))
    p = s["power"]
    state = ("UNAVAILABLE" if not p["available"] else "FAILED" if p["status"] else
             "NO SAMPLES" if not p["count"] else "STALE" if age_ms(s["ms"], p["t"]) > 500 else
             "INVALID DATA" if not p["valid"] & 1 else "RESPONDING")
    rails = "; ".join(f"{name}={p['mv'][i] / 1000:.3f} V" if p["valid"] & (1 << i) else f"{name}=invalid"
                      for i, name in enumerate(("3V3", "PWM", "5V", "VIN_PROT", "ARM")))
    reference = (f"; ADC3 {IO_REFERENCE_FAILURE_STAGES[p['ref_stage']]} on "
                 f"{'temperature' if p['ref_channel'] else 'VREFINT'}, raw {p['ref_raw']}, "
                 f"HAL {p['ref_hal_status']}/0x{p['ref_hal_error']:08X}; "
                 f"external ADC errors {p['adc_errors']}")
    rows.append(("Power ADC (verify DMM)", state, str(age_ms(s["ms"], p["t"])) if p["count"] else "--",
                 rails + reference))
    led = s.get("led")
    led_detail = (f"; RGB firmware-inhibited {led['inhibited']}, commanded/gates "
                  f"0x{led['commanded']:X}/0x{led['gates']:X}"
                  if isinstance(led, dict) else "")
    rows.append(("GPIO / switch", "OBSERVING" if p["available"] else "UNAVAILABLE", "--", f"IN bits 0x{s['gpio']['inputs']:02X}; "
                 f"OUT commanded 0x{s['gpio']['outputs']:02X}; switch {s['gpio']['switch']}"
                 f"{led_detail}; HIGH requires loopback/scope"))
    return rows
