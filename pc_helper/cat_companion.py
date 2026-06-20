"""
Desk Cat - PC helper (cross-platform: Windows / macOS / Linux).

Streams your activity to the ESP32 over USB serial, ~6x/sec:
    idle=<seconds since last input>  k=<typing 0/1>  m=<mouse 0/1>  t=<ISO time>

  - idle: drives the cat's active vs nap/break behavior; if detection fails,
    the helper sends an away/unknown value instead of pretending you're active.
  - k / m: precise keyboard-vs-mouse on Windows. macOS/Linux only expose a single
    idle timer without extra permissions/deps, so there both flags just reflect
    "input happened just now".

Privacy: the helper checks only WHETHER input happened (a 0/1 flag), never which
keys - no keystrokes or content are recorded, stored, or sent anywhere.

Run:   python cat_companion.py                  (auto-detects the board's port)
       python cat_companion.py COM7              (Windows)
       python cat_companion.py /dev/ttyUSB0      (Linux)
       python cat_companion.py /dev/tty.usbserial-XXXX   (macOS)

Needs: pip install pyserial
       macOS: nothing extra (uses the built-in `ioreg`).
       Linux: `xprintidle` for idle on X11 (e.g. sudo apt install xprintidle).
"""
import sys
import time
import platform
import subprocess
import datetime

import serial
from serial.tools import list_ports

BAUD = 115200
POLL = 0.15      # seconds between samples
WINDOW = 0.5     # treat input as "active" for this long after it happens
UNKNOWN_IDLE_SECONDS = 65535

SYSTEM = platform.system()   # 'Windows' | 'Darwin' | 'Linux'

USB_SERIAL_IDS = {
    (0x1A86, 0x7523),  # WCH CH340
    (0x1A86, 0x55D4),  # WCH CH9102
    (0x10C4, 0xEA60),  # Silicon Labs CP210x
    (0x0403, 0x6001),  # FTDI FT232
    (0x303A, 0x1001),  # Espressif USB serial/JTAG
}
PORT_HINTS = ("CH340", "CH341", "CH910", "CP210", "SILICON LABS", "FTDI",
              "USB SERIAL", "USB-SERIAL", "UART", "ESP32", "WCH", "USBSERIAL")

# ---------------------------------------------------------------------------
#  per-OS idle / activity detection
# ---------------------------------------------------------------------------
if SYSTEM == "Windows":
    import ctypes
    from ctypes import wintypes
    _user32 = ctypes.windll.user32
    _kernel32 = ctypes.windll.kernel32

    class _LASTINPUT(ctypes.Structure):
        _fields_ = [("cbSize", wintypes.UINT), ("dwTime", wintypes.DWORD)]

    class _POINT(ctypes.Structure):
        _fields_ = [("x", wintypes.LONG), ("y", wintypes.LONG)]

    def idle_seconds():
        info = _LASTINPUT(); info.cbSize = ctypes.sizeof(info)
        if not _user32.GetLastInputInfo(ctypes.byref(info)):
            return None
        return max(0, (_kernel32.GetTickCount() - info.dwTime) // 1000)

    _state = {"pos": None, "kbd": 0.0, "mouse": 0.0}

    def _cursor_pos():
        p = _POINT(); _user32.GetCursorPos(ctypes.byref(p)); return (p.x, p.y)

    def activity_flags(idle):
        now = time.monotonic()
        pos = _cursor_pos()
        if _state["pos"] is not None and pos != _state["pos"]:
            _state["mouse"] = now
        _state["pos"] = pos
        if any(_user32.GetAsyncKeyState(b) & 0x8000 for b in (0x01, 0x02, 0x04)):
            _state["mouse"] = now
        # privacy: we only test IF any key is down (a boolean); the specific key
        # is never read, recorded, or transmitted.
        for vk in range(0x08, 0xFF):
            if _user32.GetAsyncKeyState(vk) & 0x8001:
                _state["kbd"] = now
                break
        k = 1 if now - _state["kbd"] < WINDOW else 0
        m = 1 if now - _state["mouse"] < WINDOW else 0
        return k, m

elif SYSTEM == "Darwin":
    _warned = [False]

    def idle_seconds():
        try:
            out = subprocess.check_output(["ioreg", "-c", "IOHIDSystem"], text=True, timeout=1)
        except Exception:
            if not _warned[0]:
                print("  (macOS idle detection failed; sending away/unknown until it recovers)")
                _warned[0] = True
            return None
        vals = []
        for line in out.splitlines():
            if "HIDIdleTime" in line:
                try:
                    vals.append(int(line.rsplit("=", 1)[1].strip()))
                except (ValueError, IndexError):
                    pass
        if not vals:
            if not _warned[0]:
                print("  (macOS HIDIdleTime not found; sending away/unknown until it recovers)")
                _warned[0] = True
            return None
        return min(vals) // 1_000_000_000

    def activity_flags(idle):
        a = 1 if idle is not None and idle < 1 else 0
        return a, a

else:  # Linux / other
    _warned = [False]

    def idle_seconds():
        try:
            return int(subprocess.check_output(["xprintidle"], timeout=1).strip()) // 1000
        except Exception:
            if not _warned[0]:
                print("  (Linux idle detection needs 'xprintidle' on X11 - "
                      "e.g. sudo apt install xprintidle)")
                _warned[0] = True
            return None

    def activity_flags(idle):
        a = 1 if idle is not None and idle < 1 else 0
        return a, a


def describe_ports():
    ports = []
    for p in list_ports.comports():
        vidpid = f"{p.vid:04X}:{p.pid:04X}" if p.vid is not None and p.pid is not None else "no VID:PID"
        ports.append(f"{p.device} {vidpid} {p.description or ''}".strip())
    return "; ".join(ports)


def find_port(preferred):
    if preferred:
        return preferred
    ports = list(list_ports.comports())
    for p in ports:
        if p.vid is not None and p.pid is not None and (p.vid, p.pid) in USB_SERIAL_IDS:
            return p.device
    for p in ports:
        haystack = " ".join(str(v or "") for v in (p.description, p.manufacturer, p.product)).upper()
        if any(hint in haystack for hint in PORT_HINTS):
            return p.device
    return None


def stream(ser) -> None:
    while True:
        detected_idle = idle_seconds()
        k, m = activity_flags(detected_idle)
        idle = detected_idle if detected_idle is not None else UNKNOWN_IDLE_SECONDS
        ts = datetime.datetime.now().strftime("%Y-%m-%dT%H:%M:%S")
        ser.write(f"idle={idle} k={k} m={m} t={ts}\n".encode())
        ser.flush()
        while ser.in_waiting:                       # echo the cat's heartbeat back
            line = ser.readline().decode(errors="replace").strip()
            if line:
                print(f"  cat> {line}")
        time.sleep(POLL)


def main() -> None:
    preferred = sys.argv[1] if len(sys.argv) > 1 else None
    print(f"Desk Cat helper running on {SYSTEM}. Ctrl+C to stop.")
    while True:
        port = find_port(preferred)
        if not port:
            seen = describe_ports()
            print(f"  ...no supported USB serial board found. Seen: {seen or 'no serial ports'}")
            time.sleep(2)
            continue
        try:
            ser = serial.Serial()
            ser.port = port
            ser.baudrate = BAUD
            ser.timeout = 1
            ser.dtr = False          # don't reset/stall the ESP32 when we open the port
            ser.rts = False
            ser.open()
            try:
                print(f"  connected to the cat on {port}")
                stream(ser)
            finally:
                ser.close()
        except serial.SerialException as exc:
            print(f"  lost {port} ({exc}); retrying...")
            time.sleep(2)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nbye!")
