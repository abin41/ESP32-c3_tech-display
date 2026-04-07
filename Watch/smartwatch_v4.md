# ⌚ SmartWatch Firmware v4.5 — Documentation

> **TRUE SETUP EDITION**  
> Hardware: ESP32-C3 Super Mini | Display: SH1106 1.3" OLED (I2C)

---

## Hardware Pinout

| Pin | Function |
|-----|----------|
| GPIO 3 | TTP223 Touch Sensor |
| GPIO 6 | SDA (OLED I2C) |
| GPIO 7 | SCL (OLED I2C) |

---

## What's New in v4.5

- Removed hardcoded Wi-Fi defaults
- On first boot, the watch instantly enters **AP Mode** (`SmartWatch-v4`) for immediate setup via browser
- No credentials stored = no silent failures

---

## First-Time Setup

1. Power on the watch
2. On your phone or PC, connect to Wi-Fi network: **`SmartWatch-v4`** (no password)
3. Open a browser and go to **`192.168.4.1`**
4. Enter your home Wi-Fi SSID and password in the **Watch Wi-Fi Setup** card
5. Tap **Save & Reboot Watch** — the watch restarts and connects to your network
6. The OLED will show the assigned IP address (e.g., `192.168.1.x`)
7. Connect your phone back to your home Wi-Fi and open that IP in a browser

---

## Watch Faces

Cycle through faces with a **single tap** on the touch sensor, or use the web UI.

| Face | Description |
|------|-------------|
| 🕐 Clock | Live time (12H / 24H), date, and day |
| ⏱ Stopwatch | Elapsed time with lap support |
| ⏳ Timer | Countdown timer with progress bar |
| 💬 Message | Displays a custom text message (up to 20 chars) |
| ⚙️ Settings | Shows IP address and mode (AP / Connected) |

---

## Touch Controls

| Gesture | Action |
|---------|--------|
| Single tap | Advance to next watch face |
| Double tap | Return to Clock face |
| Long press (0.6s) — on Stopwatch | Start / Pause stopwatch |
| Long press (0.6s) — on Timer | Start / Pause countdown timer |
| Long press (0.6s) — on Settings | Reboot the watch |

> A **"< Back to Clock"** banner briefly appears after double-tap or long-press actions.

---

## Web Interface Features

Access the full control panel at the watch's IP address in any browser.

### Watch Face Switcher
Click any face chip to jump to it instantly.

### Wi-Fi Setup
Enter a new SSID and password. The watch saves credentials to flash and reboots.

### Stopwatch
- Start / Pause / Reset
- Record laps (up to 20)
- Display syncs with the OLED in real time

### Countdown Timer
- Set hours, minutes, seconds manually
- Quick presets: 1 min, 5 min, 10 min, 30 min
- Progress bar on both web UI and OLED

### Message Display
- Send up to **20 characters** to the Message face
- Tap **Clear** to dismiss and return to Clock

### Display Settings
| Setting | Range | Default |
|---------|-------|---------|
| Brightness | 1 – 10 | 8 |
| Sleep delay | 5 – 60 s | 20 s |
| Clock format | 12H / 24H | 24H |

---

## Connectivity

| Mode | Condition | SSID | IP |
|------|-----------|------|----|
| AP (Setup) | No saved credentials, or Wi-Fi unreachable | `SmartWatch-v4` | `192.168.4.1` |
| Station | Successfully connected to home Wi-Fi | Your network | DHCP assigned |

- WebSocket server runs on **port 81**
- HTTP server runs on **port 80**
- NTP time sync uses `pool.ntp.org` at **UTC+5:30 (IST)**

---

## Screen Sleep

The OLED turns off after the configured sleep delay (default 20 s) with no activity.  
Any touch wakes it immediately. The display refresh rate adapts to the active face:

| Condition | Refresh Rate |
|-----------|-------------|
| Clock / idle | 1 s |
| Banner animation | 50 ms |
| Stopwatch running | 50 ms |
| Timer running | 200 ms |

---

## Libraries Required

```
Arduino (ESP32-C3 core)
WiFi
WebServer
WebSocketsServer  (Links2004/arduinoWebSockets)
Wire
U8g2lib           (olikraus/u8g2)
Preferences
```

---

## Build & Flash

1. Open the `.ino` / `.cpp` file in Arduino IDE or PlatformIO
2. Select board: **ESP32C3 Dev Module**
3. Set upload speed: **115200**
4. Flash and open Serial Monitor to watch boot logs
5. On first boot, connect to `SmartWatch-v4` and complete Wi-Fi setup

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| OLED stays blank | Check SDA/SCL wiring (GPIO 6/7) and I2C address |
| Stuck in AP mode | Saved SSID/password may be wrong — re-enter via web UI |
| Touch not responding | Verify `TOUCH_PIN` wiring; check `INPUT_PULLDOWN` is correct for your sensor |
| Time shows "Syncing NTP…" | Watch isn't connected to internet; fix Wi-Fi credentials |
| Can't reach web UI | Confirm your phone is on the same network as the watch |
