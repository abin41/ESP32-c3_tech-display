/*
 * CLAWD MOCHI ALL-IN-ONE
 * ESP32-C3 Super Mini + 1.3" OLED SH1106 (I2C)
 *
 *  Wiring:
 *    OLED SDA → GPIO 6
 *    OLED SCL → GPIO 7
 *    Touch SIG → GPIO 3
 *    OLED VCC / Touch VCC → 3V3
 *    OLED GND / Touch GND → GND
 *
 *  Modes (touch cycles through):
 *    0 = Eyes (wiggle/blink animation)
 *    1 = Clock
 *    2 = Stopwatch
 *    3 = Claude Code terminal (WiFi web UI)
 *
 *  Touch behaviour:
 *    SHORT tap          → next mode (in Eyes/Clock/Terminal)
 *    SHORT tap          → pause stopwatch (when running)
 *    DOUBLE tap         → start/resume stopwatch (when idle/paused)
 *    LONG press (>700ms)→ reset stopwatch (when in SW mode)
 *                       → next mode (when NOT in SW mode, same as short)
 *
 *  WiFi AP: "ClaWD-Mochi"  pw: clawd1234 → http://192.168.4.1
 *  Library: U8g2 by olikraus
 */

#include <U8g2lib.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <math.h>

// ── Pins ──────────────────────────────────────────────────────
#define OLED_SDA  6
#define OLED_SCL  7
#define TOUCH_PIN 3

// ── OLED ──────────────────────────────────────────────────────
U8G2_SH1106_128X64_NONAME_F_HW_I2C
  u8g2(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);

// ── WiFi ──────────────────────────────────────────────────────
const char* AP_SSID = "ClaWD-Mochi";
const char* AP_PASS = "clawd1234";
WebServer server(80);

// ═════════════════════════════════════════════════════════════
//  MODE
// ═════════════════════════════════════════════════════════════
#define MODE_EYES  0
#define MODE_CLOCK 1
#define MODE_SW    2
#define MODE_CODE  3
#define MODE_COUNT 4

uint8_t currentMode = MODE_EYES;

// ═════════════════════════════════════════════════════════════
//  TOUCH STATE MACHINE
// ═════════════════════════════════════════════════════════════
bool     lastTouched      = false;
bool     longFired        = false;
bool     waitDoubleTap    = false;
unsigned long touchDownAt   = 0;
unsigned long lastReleaseAt = 0;

const unsigned long LONG_MS   = 700;
const unsigned long DOUBLE_MS = 350;

// ═════════════════════════════════════════════════════════════
//  EYES STATE
// ═════════════════════════════════════════════════════════════
#define EYE_W  14
#define EYE_H  28
#define EYE_GAP 30

unsigned long eyeLastAnim = 0;
int  eyeAnimStep  = 0;
bool eyeBlinking  = false;
int  eyeOffsets[] = {-8, 8, -8, 8, 0};
unsigned long eyeNextBlink = 3000;
unsigned long eyeBlinkStart= 0;
uint8_t eyeBlinkPhase = 0; // 0=open,1=closed,2=open2,3=closed2,4=open3

// ═════════════════════════════════════════════════════════════
//  CLOCK STATE
// ═════════════════════════════════════════════════════════════
unsigned long clockLastMs = 0;
int  clkSec  = 0, clkMin = 0, clkHr = 10;
int  clkDay  = 1, clkDate = 6;
bool colonVis = true;
unsigned long lastColonMs = 0;
const char* DAY_NAMES[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};

// ═════════════════════════════════════════════════════════════
//  STOPWATCH STATE
// ═════════════════════════════════════════════════════════════
enum SwState { SW_IDLE, SW_RUNNING, SW_PAUSED };
SwState  swState     = SW_IDLE;
unsigned long swElapsed   = 0;
unsigned long swStartedAt = 0;
int  sw_ms = 0, sw_sec = 0, sw_min = 0;
int  arcAngle = 0;
unsigned long lastArcMs = 0;
bool swColonVis  = true;
unsigned long swLastBlink = 0;

// ═════════════════════════════════════════════════════════════
//  TERMINAL STATE
// ═════════════════════════════════════════════════════════════
#define TERM_COLS 10
#define TERM_ROWS  4
bool    termMode = false;
String  termLines[TERM_ROWS];
uint8_t termRow = 0, termCol = 0;

// ═════════════════════════════════════════════════════════════
//  HELPERS
// ═════════════════════════════════════════════════════════════

void flashInvert() {
  u8g2.setDrawColor(2);
  u8g2.drawBox(0, 0, 128, 64);
  u8g2.sendBuffer();
  delay(50);
  u8g2.setDrawColor(1);
}

// ═════════════════════════════════════════════════════════════
//  EYES DRAWING
// ═════════════════════════════════════════════════════════════

int eyeLX(int ox = 0) { return (128 - (EYE_W * 2 + EYE_GAP)) / 2 + ox; }
int eyeRX(int ox = 0) { return eyeLX(ox) + EYE_W + EYE_GAP; }
int eyeTopY()         { return (64 - EYE_H) / 2 - 4; }
int eyeCY()           { return eyeTopY() + EYE_H / 2; }

void drawEyes(int ox = 0, bool blink = false) {
  u8g2.clearBuffer();
  int lx = eyeLX(ox), rx = eyeRX(ox), ey = eyeTopY();
  if (!blink) {
    u8g2.drawBox(lx, ey, EYE_W, EYE_H);
    u8g2.drawBox(rx, ey, EYE_W, EYE_H);
  } else {
    u8g2.drawBox(lx, ey + EYE_H/2 - 2, EYE_W, 4);
    u8g2.drawBox(rx, ey + EYE_H/2 - 2, EYE_W, 4);
  }
  u8g2.sendBuffer();
}

// Non-blocking eye animation tick
void tickEyes() {
  unsigned long now = millis();

  // Blink sequence (non-blocking)
  if (eyeBlinking) {
    // Each blink phase lasts ~100ms
    if (now - eyeBlinkStart >= 100) {
      eyeBlinkStart = now;
      eyeBlinkPhase++;
      if (eyeBlinkPhase == 1)      drawEyes(0, true);
      else if (eyeBlinkPhase == 2) drawEyes(0, false);
      else if (eyeBlinkPhase == 3) drawEyes(0, true);
      else if (eyeBlinkPhase >= 4) {
        drawEyes(0, false);
        eyeBlinking = false;
        eyeNextBlink = now + random(2000, 5000);
      }
    }
    return;
  }

  // Wiggle animation
  if (now - eyeLastAnim >= 90) {
    eyeLastAnim = now;
    if (eyeAnimStep < 5) {
      drawEyes(eyeOffsets[eyeAnimStep]);
      eyeAnimStep++;
    } else {
      drawEyes(0);
    }
  }

  // Trigger blink periodically
  if (now >= eyeNextBlink && !eyeBlinking) {
    eyeBlinking   = true;
    eyeBlinkStart = now;
    eyeBlinkPhase = 0;
    eyeAnimStep   = 0;
    eyeLastAnim   = now + 600; // pause wiggle during blink
  }

  // Restart wiggle loop
  if (eyeAnimStep >= 5 && (now - eyeLastAnim >= 1500)) {
    eyeAnimStep = 0;
  }
}

// ═════════════════════════════════════════════════════════════
//  CLOCK DRAWING
// ═════════════════════════════════════════════════════════════

void drawClock() {
  u8g2.clearBuffer();
  u8g2.drawRFrame(0, 0, 128, 64, 6);

  char hourStr[3], minStr[3], secStr[3];
  sprintf(hourStr, "%02d", clkHr);
  sprintf(minStr,  "%02d", clkMin);
  sprintf(secStr,  "%02d", clkSec);

  // Day + date top-right
  char dayDate[10];
  sprintf(dayDate, "%s %02d", DAY_NAMES[clkDay], clkDate);
  u8g2.setFont(u8g2_font_profont10_mr);
  u8g2.drawStr(124 - u8g2.getStrWidth(dayDate), 12, dayDate);

  // HH:MM large centered
  u8g2.setFont(u8g2_font_logisoso28_tn);
  int hW = u8g2.getStrWidth(hourStr);
  int mW = u8g2.getStrWidth(minStr);
  int cW = u8g2.getStrWidth(":");
  int totalW = hW + cW + mW;
  int sx = (128 - totalW) / 2;
  u8g2.drawStr(sx, 46, hourStr);
  if (colonVis) u8g2.drawStr(sx + hW, 46, ":");
  u8g2.drawStr(sx + hW + cW, 46, minStr);

  // Seconds progress bar
  int barX = 26, barW = 90, barY = 56, barH = 3;
  u8g2.drawRFrame(barX, barY, barW, barH, 1);
  int filled = (clkSec * barW) / 59;
  if (filled > 0) u8g2.drawBox(barX, barY, filled, barH);

  u8g2.sendBuffer();
}

void tickClock() {
  unsigned long now = millis();

  // Colon blink every 500ms
  if (now - lastColonMs >= 500) {
    lastColonMs = now;
    colonVis = !colonVis;
  }

  // Second tick
  if (now - clockLastMs >= 1000) {
    clockLastMs = now;
    clkSec++;
    if (clkSec >= 60) { clkSec = 0; clkMin++; }
    if (clkMin >= 60) { clkMin = 0; clkHr++;  }
    if (clkHr  >= 24) { clkHr  = 0; clkDate++; clkDay = (clkDay+1)%7; }
  }

  drawClock();
}

// ═════════════════════════════════════════════════════════════
//  STOPWATCH DRAWING
// ═════════════════════════════════════════════════════════════

void arcDot(int cx, int cy, int r, int angle) {
  float rad = angle * 3.14159f / 180.0f;
  int px = cx + (int)(r * cosf(rad));
  int py = cy + (int)(r * sinf(rad));
  u8g2.drawBox(px-1, py-1, 3, 3);
}

void drawSwIdle() {
  u8g2.drawRFrame(0, 0, 128, 64, 6);
  u8g2.setFont(u8g2_font_profont10_mr);
  u8g2.drawStr(34, 13, "STOPWATCH");
  for (int x = 10; x < 118; x += 6) u8g2.drawBox(x, 18, 3, 1);
  u8g2.setFont(u8g2_font_logisoso28_tn);
  u8g2.drawStr(14, 50, "00:00");
  u8g2.setFont(u8g2_font_profont10_mr);
  u8g2.drawStr(20, 62, "dbl:start");
}

void drawSwRunning() {
  u8g2.drawRFrame(0, 0, 128, 64, 6);
  // Spinning arc
  int cx = 64, cy = 32, r = 29;
  for (int a = 0; a < 300; a += 20) {
    int aa = (arcAngle + a) % 360;
    float rad = aa * 3.14159f / 180.0f;
    int px = cx + (int)(r * cosf(rad));
    int py = cy + (int)(r * sinf(rad));
    int sz = (a < 60) ? 2 : 1;
    u8g2.drawBox(px - sz/2, py - sz/2, sz, sz);
  }
  arcDot(cx, cy, r, arcAngle);

  // MM:SS
  char buf[6];
  sprintf(buf, "%02d:%02d", sw_min, sw_sec);
  u8g2.setFont(u8g2_font_logisoso28_tn);
  int tw = u8g2.getStrWidth(buf);
  u8g2.drawStr((128 - tw)/2, 43, buf);

  // Centiseconds
  char ms[4];
  sprintf(ms, ".%02d", sw_ms);
  u8g2.setFont(u8g2_font_profont10_mr);
  u8g2.drawStr((128 - u8g2.getStrWidth(ms))/2 + 28, 54, ms);

  u8g2.setFont(u8g2_font_profont10_mr);
  u8g2.drawStr(22, 62, "tap:pause");
}

void drawSwPaused() {
  u8g2.drawRFrame(0, 0, 128, 64, 6);
  u8g2.setFont(u8g2_font_profont10_mr);
  u8g2.drawStr(46, 13, "PAUSED");
  for (int x = 10; x < 118; x += 6) u8g2.drawBox(x, 17, 3, 1);

  if (swColonVis) {
    char buf[6];
    sprintf(buf, "%02d:%02d", sw_min, sw_sec);
    u8g2.setFont(u8g2_font_logisoso28_tn);
    int tw = u8g2.getStrWidth(buf);
    u8g2.drawStr((128 - tw)/2, 46, buf);
    char ms[4];
    sprintf(ms, ".%02d", sw_ms);
    u8g2.setFont(u8g2_font_profont10_mr);
    u8g2.drawStr((128 - u8g2.getStrWidth(ms))/2 + 28, 57, ms);
  }

  u8g2.setFont(u8g2_font_profont10_mr);
  u8g2.drawStr(2, 62, "dbl:go  hold:rst");
}

void tickStopwatch() {
  unsigned long now = millis();

  // Update elapsed
  if (swState == SW_RUNNING) {
    swElapsed = now - swStartedAt;
    sw_ms  = (swElapsed % 1000) / 10;
    sw_sec = (swElapsed / 1000) % 60;
    sw_min = (swElapsed / 60000) % 60;
  }

  // Spin arc
  if (swState == SW_RUNNING && now - lastArcMs >= 40) {
    lastArcMs = now;
    arcAngle = (arcAngle + 6) % 360;
  }

  // Blink when paused
  if (now - swLastBlink >= 500) {
    swLastBlink = now;
    swColonVis = !swColonVis;
  }

  u8g2.clearBuffer();
  switch(swState) {
    case SW_IDLE:    drawSwIdle();    break;
    case SW_RUNNING: drawSwRunning(); break;
    case SW_PAUSED:  drawSwPaused();  break;
  }
  u8g2.sendBuffer();
}

// ═════════════════════════════════════════════════════════════
//  TERMINAL
// ═════════════════════════════════════════════════════════════

void termClear() {
  for (uint8_t i = 0; i < TERM_ROWS; i++) termLines[i] = "";
  termRow = 0; termCol = 0;
}

void termDraw() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(0, 8, "clawd@mochi $");
  u8g2.drawHLine(0, 10, 128);
  for (uint8_t r = 0; r < TERM_ROWS; r++) {
    int yy = 22 + r * 12;
    String prefix = (r == termRow) ? ">" : " ";
    String line   = prefix + " " + termLines[r];
    u8g2.drawStr(0, yy, line.c_str());
    if (r == termRow) {
      int cx = (line.length()) * 6;
      u8g2.drawBox(cx, yy - 8, 5, 9);
    }
  }
  u8g2.sendBuffer();
}

void termAddChar(char c) {
  if (c == '\n' || c == '\r') {
    termRow++;
    termCol = 0;
    if (termRow >= TERM_ROWS) {
      for (uint8_t i = 0; i < TERM_ROWS-1; i++) termLines[i] = termLines[i+1];
      termLines[TERM_ROWS-1] = "";
      termRow = TERM_ROWS-1;
    }
  } else if ((c == '\b' || c == 127) && termCol > 0) {
    termCol--;
    termLines[termRow].remove(termLines[termRow].length()-1);
  } else if (c >= 32 && c < 127 && termCol < TERM_COLS) {
    termLines[termRow] += c;
    termCol++;
  }
  termDraw();
}

void drawCodeView() {
  u8g2.clearBuffer();
  u8g2.drawHLine(0, 0, 128);
  u8g2.drawHLine(0, 63, 128);
  u8g2.setFont(u8g2_font_ncenB14_tr);
  u8g2.drawStr(20, 30, "Claude");
  u8g2.setFont(u8g2_font_ncenB10_tr);
  u8g2.drawStr(36, 50, "Code");
  u8g2.drawHLine(36, 53, 44);
  u8g2.sendBuffer();
}

// ═════════════════════════════════════════════════════════════
//  SWITCH MODE
// ═════════════════════════════════════════════════════════════

void switchToMode(uint8_t m) {
  currentMode = m;
  termMode    = false;

  switch(m) {
    case MODE_EYES:
      eyeAnimStep  = 0;
      eyeBlinking  = false;
      eyeNextBlink = millis() + 3000;
      drawEyes(0, false);
      break;

    case MODE_CLOCK:
      clockLastMs = millis();
      lastColonMs = millis();
      drawClock();
      break;

    case MODE_SW:
      // Don't reset stopwatch on mode switch — just show current state
      u8g2.clearBuffer();
      switch(swState){
        case SW_IDLE:    drawSwIdle();    break;
        case SW_RUNNING: drawSwRunning(); break;
        case SW_PAUSED:  drawSwPaused();  break;
      }
      u8g2.sendBuffer();
      break;

    case MODE_CODE:
      drawCodeView();
      delay(800);
      termMode = true;
      termClear();
      termDraw();
      break;
  }
}

void nextMode() {
  flashInvert();
  switchToMode((currentMode + 1) % MODE_COUNT);
}

// ═════════════════════════════════════════════════════════════
//  TOUCH HANDLER (unified)
// ═════════════════════════════════════════════════════════════

// Called once per loop — handles short/double/long for all modes
void handleTouch() {
  bool touched = (digitalRead(TOUCH_PIN) == HIGH);
  unsigned long now = millis();

  // ── Press start ──
  if (touched && !lastTouched) {
    touchDownAt = now;
    longFired   = false;
  }

  // ── Long press detection (while held) ──
  if (touched && lastTouched && !longFired) {
    if (now - touchDownAt >= LONG_MS) {
      longFired = true;
      if (currentMode == MODE_SW) {
        // Long press in SW mode → RESET
        swState   = SW_IDLE;
        swElapsed = 0;
        sw_ms = sw_sec = sw_min = 0;
        arcAngle = 0;
        flashInvert();
      } else {
        // Long press anywhere else → next mode (same as short)
        nextMode();
      }
    }
  }

  // ── Release ──
  if (!touched && lastTouched && !longFired) {
    unsigned long dur = now - touchDownAt;
    if (dur < LONG_MS) {
      if (currentMode == MODE_SW) {
        // Check double tap
        if (waitDoubleTap && (now - lastReleaseAt < DOUBLE_MS)) {
          waitDoubleTap = false;
          // DOUBLE TAP → start/resume
          if (swState == SW_IDLE || swState == SW_PAUSED) {
            swState     = SW_RUNNING;
            swStartedAt = millis() - swElapsed;
            flashInvert();
          }
        } else {
          waitDoubleTap  = true;
          lastReleaseAt  = now;
        }
      } else {
        // Not SW mode: short tap = next mode (with double-tap guard)
        if (waitDoubleTap && (now - lastReleaseAt < DOUBLE_MS)) {
          waitDoubleTap = false;
          // double tap outside SW = still just next mode
          nextMode();
        } else {
          waitDoubleTap = true;
          lastReleaseAt = now;
        }
      }
    }
  }

  // ── Single tap timeout ──
  if (waitDoubleTap && (now - lastReleaseAt > DOUBLE_MS)) {
    waitDoubleTap = false;
    if (currentMode == MODE_SW) {
      // Single tap in SW → PAUSE if running
      if (swState == SW_RUNNING) {
        swState   = SW_PAUSED;
        swElapsed = millis() - swStartedAt;
      }
    } else {
      // Single tap outside SW → next mode
      nextMode();
    }
  }

  lastTouched = touched;
}

// ═════════════════════════════════════════════════════════════
//  WEB PAGE
// ═════════════════════════════════════════════════════════════

const char INDEX_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>Clawd Mochi</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}
body{background:#1c1c20;font-family:'Courier New',monospace;color:#e8e4dc;
  display:flex;flex-direction:column;align-items:center;
  padding:20px 14px 52px;gap:14px;min-height:100vh}
.sitename{font-size:10px;color:#5a5048;margin-top:4px;letter-spacing:3px;text-align:center}
.sec{width:100%;max-width:390px;font-size:10px;color:#8a8278;letter-spacing:2px;font-weight:bold;padding:0 2px}
.busy{width:100%;max-width:390px;height:2px;background:#2e2a28;border-radius:1px;overflow:hidden;opacity:0;transition:opacity .2s}
.busy.show{opacity:1}
.busy-i{height:100%;width:30%;background:#c96a3e;animation:sl 1s linear infinite}
@keyframes sl{0%{margin-left:-30%}100%{margin-left:100%}}
.vgrid{display:grid;grid-template-columns:1fr 1fr;gap:8px;width:100%;max-width:390px}
.vbtn{background:#252428;border:1.5px solid #38343a;border-radius:12px;color:#d8d4cc;
  font-family:'Courier New',monospace;padding:14px 6px 10px;cursor:pointer;text-align:center;transition:all .12s}
.vbtn:active:not(:disabled){transform:scale(.94)}
.vbtn:disabled{opacity:.3;cursor:default}
.vbtn .ic{font-size:18px;display:block;margin-bottom:4px;color:#c96a3e}
.vbtn .nm{font-size:12px;font-weight:bold;color:#e8e4dc}
.vbtn .ht{font-size:9px;color:#8a8278;margin-top:3px}
.vbtn.active{border-color:#c96a3e;background:#201408}
.swbtns{display:flex;gap:8px;width:100%;max-width:390px}
.swb{flex:1;background:#252428;border:1.5px solid #38343a;border-radius:10px;color:#b8b4ac;
  font-family:'Courier New',monospace;font-size:12px;font-weight:bold;
  padding:12px 4px;cursor:pointer;text-align:center;transition:all .12s}
.swb:active{transform:scale(.94)}
.swb.go{border-color:#28b878;color:#28b878}
.swb.pause{border-color:#c96a3e;color:#c96a3e}
.swb.rst{border-color:#555;color:#888}
.speed-row{width:100%;max-width:390px;display:flex;align-items:center;gap:10px}
.sl{font-size:10px;color:#6a6058;white-space:nowrap}
input[type=range]{flex:1;accent-color:#c96a3e;cursor:pointer;height:20px}
.sv{font-size:11px;color:#c96a3e;min-width:44px;text-align:right;font-weight:bold}
.twrap{width:100%;max-width:390px;display:none;flex-direction:column;gap:8px}
.twrap.open{display:flex}
.thdr{display:flex;justify-content:space-between;align-items:center}
.tttl{font-size:11px;color:#28b878;letter-spacing:1px;font-weight:bold}
.tx{background:#0c1e12;border:2px solid #1a4828;border-radius:9px;color:#28b878;
  font-family:'Courier New',monospace;font-size:13px;font-weight:bold;padding:10px 14px;cursor:pointer}
.trow{display:flex;gap:6px}
.tin{flex:1;background:#0c1018;border:1.5px solid #1a2820;border-radius:9px;
  color:#40d880;font-family:'Courier New',monospace;font-size:15px;padding:11px;outline:none}
.tgo{background:#1a9060;border:none;border-radius:9px;color:#fff;
  font-family:'Courier New',monospace;font-size:22px;font-weight:bold;
  padding:11px 16px;cursor:pointer;min-width:52px}
.toast{position:fixed;bottom:18px;left:50%;transform:translateX(-50%);
  background:#252428;border:1.5px solid #38343a;border-radius:9px;
  font-size:12px;color:#d8d4cc;padding:7px 16px;opacity:0;
  transition:opacity .18s;pointer-events:none;white-space:nowrap;z-index:99}
.toast.show{opacity:1}
.clkrow{width:100%;max-width:390px;display:flex;gap:8px}
.ci{display:flex;flex-direction:column;gap:4px;flex:1}
.cl{font-size:10px;color:#8a8278;letter-spacing:1px;font-weight:bold;text-align:center}
.cn{background:#252428;border:1.5px solid #38343a;border-radius:8px;
  color:#e8e4dc;font-family:'Courier New',monospace;font-size:14px;
  padding:8px;width:100%;outline:none;text-align:center}
</style>
</head>
<body>
<div style="font-size:26px;color:#c96a3e;font-weight:bold;text-align:center">[ @ @ ]</div>
<div class="sitename">CLAWD &middot; MOCHI &middot; ALL-IN-ONE</div>

<div class="busy" id="busy"><div class="busy-i"></div></div>

<div class="sec">// mode</div>
<div class="vgrid">
  <button class="vbtn active" data-m="0" onclick="setMode(0)">
    <span class="ic">&#9632; &#9632;</span><span class="nm">Eyes</span><span class="ht">wiggle + blink</span>
  </button>
  <button class="vbtn" data-m="1" onclick="setMode(1)">
    <span class="ic">&#9719;</span><span class="nm">Clock</span><span class="ht">tap to set time</span>
  </button>
  <button class="vbtn" data-m="2" onclick="setMode(2)">
    <span class="ic">&#9654;</span><span class="nm">Stopwatch</span><span class="ht">start/pause/reset</span>
  </button>
  <button class="vbtn" data-m="3" onclick="setMode(3)">
    <span class="ic">{ }</span><span class="nm">Terminal</span><span class="ht">type on screen</span>
  </button>
</div>

<div class="sec">// stopwatch controls</div>
<div class="swbtns">
  <button class="swb go"    onclick="swCmd('start')">&#9654; start</button>
  <button class="swb pause" onclick="swCmd('pause')">&#9646;&#9646; pause</button>
  <button class="swb rst"   onclick="swCmd('reset')">&#9632; reset</button>
</div>

<div class="sec">// set clock time</div>
<div class="clkrow">
  <div class="ci"><span class="cl">HR</span><input class="cn" id="cHr"  type="number" min="0" max="23" value="10"></div>
  <div class="ci"><span class="cl">MIN</span><input class="cn" id="cMin" type="number" min="0" max="59" value="0"></div>
  <div class="ci"><span class="cl">SEC</span><input class="cn" id="cSec" type="number" min="0" max="59" value="0"></div>
  <div class="ci"><span class="cl">DAY</span>
    <select class="cn" id="cDay">
      <option value="0">Sun</option><option value="1">Mon</option>
      <option value="2">Tue</option><option value="3">Wed</option>
      <option value="4">Thu</option><option value="5">Fri</option>
      <option value="6">Sat</option>
    </select>
  </div>
  <div class="ci"><span class="cl">DATE</span><input class="cn" id="cDate" type="number" min="1" max="31" value="6"></div>
</div>
<button class="swb go" style="width:100%;max-width:390px;margin-top:4px" onclick="setTime()">&#9654; set time</button>

<div class="sec">// terminal</div>
<div class="twrap" id="twrap">
  <div class="thdr">
    <span class="tttl">&#9658; clawd:~$</span>
    <button class="tx" onclick="closeTerm()">&#x2715; exit</button>
  </div>
  <div class="trow">
    <input class="tin" id="tin" type="text" placeholder="type here..."
      autocomplete="off" autocorrect="off" autocapitalize="off" spellcheck="false">
    <button class="tgo" onclick="termEnter()">&#8629;</button>
  </div>
</div>

<div class="toast" id="toast"></div>

<script>
let activeMode=0, termOpen=false, isBusy=false, tt;
function toast(msg,ok=true){
  const el=document.getElementById('toast');
  el.textContent=msg; el.style.borderColor=ok?'#28b878':'#c96a3e';
  el.classList.add('show'); clearTimeout(tt);
  tt=setTimeout(()=>el.classList.remove('show'),1400);
}
async function req(path){
  try{const r=await fetch(path);return r.ok;}
  catch(e){toast('no connection',false);return false;}
}
async function setMode(m){
  const keys=['w','c','t','d'];
  if(!await req('/cmd?k='+keys[m])) return;
  activeMode=m;
  document.querySelectorAll('.vbtn').forEach(b=>b.classList.toggle('active',parseInt(b.dataset.m)===m));
  if(m===3){
    termOpen=true;
    document.getElementById('twrap').classList.add('open');
    document.getElementById('tin').focus();
    toast('terminal open');
  } else {
    termOpen=false;
    document.getElementById('twrap').classList.remove('open');
  }
  toast(['eyes','clock','stopwatch','terminal'][m]);
}
async function swCmd(a){
  await req('/sw?a='+a);
  toast(a);
}
async function setTime(){
  const h=document.getElementById('cHr').value;
  const m=document.getElementById('cMin').value;
  const s=document.getElementById('cSec').value;
  const dy=document.getElementById('cDay').value;
  const dt=document.getElementById('cDate').value;
  await req('/settime?h='+h+'&m='+m+'&s='+s+'&dy='+dy+'&dt='+dt);
  toast('time set!');
}
const tin=document.getElementById('tin');
let lastVal='';
tin.addEventListener('input',async()=>{
  const cur=tin.value,prev=lastVal;
  if(cur.length>prev.length) await req('/char?c='+encodeURIComponent(cur[cur.length-1]));
  else if(cur.length<prev.length) await req('/char?c=%08');
  lastVal=cur;
});
async function termEnter(){await req('/char?c=%0A');tin.value='';lastVal='';tin.focus();}
tin.addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();termEnter();}});
async function closeTerm(){
  termOpen=false;
  document.getElementById('twrap').classList.remove('open');
  await req('/cmd?k=q');
  toast('terminal closed');
}
</script>
</body>
</html>
)rawhtml";

// ═════════════════════════════════════════════════════════════
//  WEB ROUTES
// ═════════════════════════════════════════════════════════════

void routeRoot() {
  server.sendHeader("Cache-Control","no-store");
  server.send_P(200,"text/html",INDEX_HTML);
}

void routeCmd() {
  if (!server.hasArg("k")){server.send(400,"text/plain","bad");return;}
  char c = server.arg("k")[0];

  if (termMode && c != 'q') {
    server.send(200,"application/json","{\"ok\":1}"); return;
  }

  server.send(200,"application/json","{\"ok\":1}");
  switch(c) {
    case 'w': switchToMode(MODE_EYES);  break;
    case 'c': switchToMode(MODE_CLOCK); break;
    case 't': switchToMode(MODE_SW);    break;
    case 'd':
      switchToMode(MODE_CODE);
      break;
    case 'q':
      termMode = false;
      switchToMode(MODE_EYES);
      break;
  }
}

void routeChar() {
  if (!termMode){server.send(200,"application/json","{\"ok\":1}");return;}
  String val = server.arg("c");
  if (val.length() > 0) termAddChar(val[0]);
  server.send(200,"application/json","{\"ok\":1}");
}

void routeSw() {
  String a = server.arg("a");
  if (a == "start") {
    if (swState == SW_IDLE || swState == SW_PAUSED) {
      swState     = SW_RUNNING;
      swStartedAt = millis() - swElapsed;
      if (currentMode != MODE_SW) switchToMode(MODE_SW);
    }
  } else if (a == "pause") {
    if (swState == SW_RUNNING) {
      swState   = SW_PAUSED;
      swElapsed = millis() - swStartedAt;
    }
  } else if (a == "reset") {
    swState = SW_IDLE;
    swElapsed = 0;
    sw_ms = sw_sec = sw_min = 0;
    arcAngle = 0;
  }
  server.send(200,"application/json","{\"ok\":1}");
}

void routeSetTime() {
  if (server.hasArg("h"))  clkHr   = constrain(server.arg("h").toInt(),  0, 23);
  if (server.hasArg("m"))  clkMin  = constrain(server.arg("m").toInt(),  0, 59);
  if (server.hasArg("s"))  clkSec  = constrain(server.arg("s").toInt(),  0, 59);
  if (server.hasArg("dy")) clkDay  = constrain(server.arg("dy").toInt(), 0, 6);
  if (server.hasArg("dt")) clkDate = constrain(server.arg("dt").toInt(), 1, 31);
  clockLastMs = millis();
  server.send(200,"application/json","{\"ok\":1}");
}

void routeNotFound(){ server.send(404,"text/plain","not found"); }

// ═════════════════════════════════════════════════════════════
//  SETUP
// ═════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  pinMode(TOUCH_PIN, INPUT_PULLDOWN);

  Wire.begin(OLED_SDA, OLED_SCL);
  u8g2.begin();
  u8g2.setContrast(255);
  u8g2.setBusClock(400000);

  // Boot splash
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB14_tr);
  u8g2.drawStr(18, 28, "Clawd");
  u8g2.setFont(u8g2_font_ncenB10_tr);
  u8g2.drawStr(24, 46, "Mochi");
  u8g2.drawHLine(10, 50, 108);
  u8g2.sendBuffer();
  delay(1400);

  // WiFi
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  // WiFi info screen
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(0, 10, "WiFi: ClaWD-Mochi");
  u8g2.drawStr(0, 22, "pw: clawd1234");
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 40, "192.168.4.1");
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(0, 54, "touch = next mode");
  u8g2.sendBuffer();
  delay(2000);

  // Register routes
  server.on("/",        HTTP_GET, routeRoot);
  server.on("/cmd",     HTTP_GET, routeCmd);
  server.on("/char",    HTTP_GET, routeChar);
  server.on("/sw",      HTTP_GET, routeSw);
  server.on("/settime", HTTP_GET, routeSetTime);
  server.onNotFound(routeNotFound);
  server.begin();

  // Start on eyes
  switchToMode(MODE_EYES);
  clockLastMs = millis();
  lastColonMs = millis();
}

// ═════════════════════════════════════════════════════════════
//  LOOP
// ═════════════════════════════════════════════════════════════

void loop() {
  server.handleClient();
  handleTouch();

  switch(currentMode) {
    case MODE_EYES:  tickEyes();       break;
    case MODE_CLOCK: tickClock();      break;
    case MODE_SW:    tickStopwatch();  break;
    case MODE_CODE:
      if (termMode) {
        // terminal just waits for web input, nothing to tick
      } else {
        drawCodeView();
      }
      break;
  }
}
