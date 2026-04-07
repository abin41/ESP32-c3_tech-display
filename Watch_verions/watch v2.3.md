Clawd Mochi v2
> ESP32-C3 Super Mini + 1.3" SH1106 OLED — all-in-one watch, stopwatch & terminal
---
Hardware
Component	Part
MCU	ESP32-C3 Super Mini
Display	1.3" OLED SH1106 128×64 (I2C)
Input	Capacitive touch sensor
Wiring
Signal	GPIO
OLED SDA	6
OLED SCL	7
Touch SIG	3
VCC (OLED + Touch)	3V3
GND (OLED + Touch)	GND
---
Library
Install via Arduino Library Manager:
```
U8g2 by olikraus
```
Board: ESP32C3 Dev Module (or ESP32-C3 Super Mini)
---
Modes
The device has 4 modes. Touch cycles through them in order.
```
Eyes  →  Analog  →  Stopwatch  →  (back to Eyes)
                                        ↑
                        Terminal reachable via Web UI only
```
0 — Eyes
Animated rectangular eyes with wiggle side-to-side and a double-blink every few seconds. Glint dots cut into each eye for a polished look.
1 — Analog Clock
Full analog clock face with:
Circle with 12 tick marks (longer at 3/6/9/12)
Smooth sweep second hand (thin)
Minute hand (medium weight)
Hour hand (short, thick)
Digital HH:MM + seconds + day/date readout on the right side
2 — Stopwatch
Three sub-states: Idle, Running, Paused.
Idle — shows `00:00`, hint text at bottom
Running — spinning comet arc around the time, centiseconds shown
Paused — time blinks, hints for resume/reset shown
3 — Terminal
Green-on-black terminal display driven entirely from the web UI. Shows a header bar with `clawd@mochi:~$` and 4 lines of scrollable text with a blinking block cursor.
---
Touch Controls
General
Gesture	Action
Single tap	Next mode (Eyes → Analog → Stopwatch → Eyes)
Long press >700ms	Next mode (same as single tap)
In Stopwatch mode
Gesture	State	Action
Single tap	Idle	→ Back to Eyes
Single tap	Running	→ Pause
Single tap	Paused	→ Back to Eyes
Double tap	Idle / Paused	→ Start / Resume
Long press >700ms	Any	→ Reset to 00:00
---
WiFi & Web UI
The device creates a WiFi access point on boot.
Setting	Value
SSID	`ClaWD-Mochi`
Password	`clawd1234`
Web UI	`http://192.168.4.1`
Web UI features
Mode buttons — switch between Eyes, Analog, Stopwatch, Terminal instantly
Stopwatch controls — Start / Pause / Reset buttons
Set time — set HR, MIN, SEC, DAY, DATE directly; clock keeps running from that point
Terminal — type text, it appears live on the OLED; Enter sends a new line; backspace works; Exit button closes terminal and returns to Eyes
Web API endpoints
Endpoint	Params	Action
`GET /`	—	Serves the web UI
`GET /cmd`	`k=w` eyes, `k=a` analog, `k=t` stopwatch, `k=d` terminal, `k=q` exit terminal	Switch mode
`GET /sw`	`a=start`, `a=pause`, `a=reset`	Stopwatch control
`GET /char`	`c=<char>`	Send character to terminal (backspace = `%08`, newline = `%0A`)
`GET /settime`	`h`, `m`, `s`, `dy`, `dt`	Set clock time and date
---
Boot Sequence
OLED initialises — splash screen shows eyes icon + "Clawd Mochi v2"
WiFi AP starts
WiFi info screen — SSID, password, IP shown for 2.2 seconds
Starts in Eyes mode
---
Code Structure
```
setup()
└── drawBootSplash()
└── WiFi.softAP()
└── server.begin()
└── switchToMode(MODE_EYES)

loop()
├── server.handleClient()
├── handleTouch()
└── tick function for current mode
    ├── tickEyes()
    ├── drawAnalog()
    ├── tickStopwatch()
    └── tickTerminal()
```
Key functions
Function	Purpose
`handleTouch()`	Unified touch state machine — detects single, double, long
`switchToMode(m)`	Transitions to a new mode, resets relevant state
`nextMode()`	Advances mode; from Stopwatch always returns to Eyes
`flashInvert()`	Brief screen invert flash on mode change / stopwatch action
`currentTime(h,m,s)`	Derives current H/M/S from `millis()` delta + set base time
`termAddChar(c)`	Adds character to terminal buffer, handles newline/backspace
---
Notes
Time is not battery-backed. It resets to `10:00:00 WED 07` on power loss. Use the web UI to set it after each boot.
The stopwatch keeps running if you switch away from stopwatch mode — it will resume where it left off when you return.
Terminal mode is intentionally not reachable by touch cycling — it is web-only to avoid accidentally entering it.
