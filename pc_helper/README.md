# Desk Cat - PC helper

The cat only knows you're at the computer (and what time it is) because this
little script tells it. It reads your system idle time and streams it to the
board over USB about 6 times per second. Runs on **Windows, macOS, and Linux**.

## One-time setup

```
pip install pyserial
```

Per-OS idle detection:

| OS | Needs | Notes |
|----|-------|-------|
| Windows | nothing extra | precise keyboard-vs-mouse split |
| macOS | nothing extra | uses the built-in `ioreg` |
| Linux (X11) | `xprintidle` | e.g. `sudo apt install xprintidle` |

On macOS/Linux the OS only exposes a single idle timer, so the two on-screen
keyboard/mouse indicators there both just light up when you're active.

## Run it

```
python cat_companion.py
```

It auto-detects common ESP32 USB-serial chips (CH340/CH9102, CP210x, FTDI,
Espressif USB-serial/JTAG). To force a port:

```
python cat_companion.py COM7                       # Windows
python cat_companion.py /dev/ttyUSB0               # Linux
python cat_companion.py /dev/tty.usbserial-XXXX    # macOS
```

You'll see the cat's heartbeat echoed back (`cat> [cat] link=1 ...`) — that's how
you know the board sees you. Move the mouse / type and the countdown advances;
stop for ~30s and Nyan naps and drifts. When a break is due, press the ESP32
BOOT button to start the 5:00 break.

> The helper and PlatformIO's **Upload / Serial Monitor** both need the port.
> Stop the helper (Ctrl+C) before re-flashing, then start it again.

## Make it start automatically (optional)

- **Windows:** `Win+R` → `shell:startup` → drop a shortcut to `cat_companion.bat`.
- **macOS:** add a *Login Item* (System Settings → General → Login Items) pointing
  at a small launcher, or a `~/Library/LaunchAgents` plist.
- **Linux:** add a `~/.config/autostart/deskcat.desktop` entry, or a systemd
  `--user` service running `python cat_companion.py`.
