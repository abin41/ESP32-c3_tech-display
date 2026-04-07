/*
 * CLAWD MOCHI v2
 * ESP32-C3 Super Mini + 1.3" OLED SH1106 (I2C)
 *
 *  Wiring:
 *    OLED SDA → GPIO 6
 *    OLED SCL → GPIO 7
 *    Touch SIG → GPIO 3
 *    VCC → 3V3 / GND → GND
 *
 *  Modes (touch cycles through):
 *    0 = Eyes      — wiggle + double-blink animation
 *    1 = Analog    — analog clock face with sweep hands
 *    2 = Stopwatch
 *    3 = Terminal  (WiFi web UI)
 *
 *  Touch:
 *    SHORT tap     → next mode (Eyes / Analog / Stopwatch / Terminal)
 *    SHORT tap     → pause stopwatch (when running)
 *    DOUBLE tap    → start / resume stopwatch
 *    LONG (>700ms) → reset stopwatch  (in SW mode)
 *                  → next mode        (all other modes)
 *
 *  WiFi AP: "ClaWD-Mochi"  pw: clawd1234
 *  Web UI:  http://192.168.4.1
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

// ── WiFi / Web ────────────────────────────────────────────────
const char* AP_SSID = "ClaWD-Mochi";
const char* AP_PASS = "clawd1234";
WebServer server(80);

// ═══════════════════════════════════════════════════
//  MODES
// ═══════════════════════════════════════════════════
#define MODE_EYES     0
#define MODE_ANALOG   1
#define MODE_SW       2
#define MODE_TERMINAL 3
#define MODE_COUNT    4

uint8_t currentMode = MODE_EYES;

// ═══════════════════════════════════════════════════
//  TOUCH STATE MACHINE
// ═══════════════════════════════════════════════════
bool     lastTouched    = false;
bool     longFired      = false;
bool     waitDoubleTap  = false;
unsigned long touchDownAt   = 0;
unsigned long lastReleaseAt = 0;

const unsigned long LONG_MS   = 700;
const unsigned long DOUBLE_MS = 350;

// ═══════════════════════════════════════════════════
//  TIME
// ═══════════════════════════════════════════════════
unsigned long clockBaseMs = 0;
int clkHr = 10, clkMin = 0, clkSec = 0;
int clkDay = 3, clkDate = 7;  // WED 7
const char* DAY_NAMES[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};

// returns total seconds since epoch-base
unsigned long totalSecs() {
  return (millis() - clockBaseMs) / 1000
       + (unsigned long)clkHr * 3600
       + (unsigned long)clkMin * 60
       + clkSec;
}

void currentTime(int &h, int &m, int &s) {
  unsigned long t = totalSecs();
  s = t % 60; t /= 60;
  m = t % 60; t /= 60;
  h = t % 24;
}

// ═══════════════════════════════════════════════════
//  EYES
// ═══════════════════════════════════════════════════
#define EYE_W   14
#define EYE_H   28
#define EYE_GAP 30

int  eyeOX          = 0;   // current x offset
int  eyeTargetOX    = 0;
bool eyeBlinking    = false;
uint8_t eyeBlinkPhase = 0;
unsigned long eyeBlinkStart  = 0;
unsigned long eyeNextBlink   = 3000;
unsigned long eyeLastWiggle  = 0;
int  eyeWiggleStep  = 0;
const int WIGGLE_OX[] = {-8, 8, -8, 8, 0};

void drawEyes(int ox, bool squint) {
  u8g2.clearBuffer();
  int lx = (128 - (EYE_W*2 + EYE_GAP)) / 2 + ox;
  int rx = lx + EYE_W + EYE_GAP;
  int ty = (64 - EYE_H) / 2 - 4;
  if (!squint) {
    u8g2.drawRBox(lx, ty, EYE_W, EYE_H, 3);
    u8g2.drawRBox(rx, ty, EYE_W, EYE_H, 3);
    // glint
    u8g2.setDrawColor(0);
    u8g2.drawBox(lx+2, ty+3, 3, 3);
    u8g2.drawBox(rx+2, ty+3, 3, 3);
    u8g2.setDrawColor(1);
  } else {
    int mid = ty + EYE_H/2 - 2;
    u8g2.drawRBox(lx, mid, EYE_W, 5, 2);
    u8g2.drawRBox(rx, mid, EYE_W, 5, 2);
  }
  u8g2.sendBuffer();
}

void tickEyes() {
  unsigned long now = millis();
  // blink sequence
  if (eyeBlinking) {
    if (now - eyeBlinkStart >= 90) {
      eyeBlinkStart = now;
      eyeBlinkPhase++;
      if      (eyeBlinkPhase == 1) drawEyes(0, true);
      else if (eyeBlinkPhase == 2) drawEyes(0, false);
      else if (eyeBlinkPhase == 3) drawEyes(0, true);
      else {
        drawEyes(0, false);
        eyeBlinking  = false;
        eyeNextBlink = now + random(2500, 5000);
        eyeLastWiggle = now + 500;
      }
    }
    return;
  }
  // wiggle
  if (now - eyeLastWiggle >= 85) {
    eyeLastWiggle = now;
    if (eyeWiggleStep < 5) {
      drawEyes(WIGGLE_OX[eyeWiggleStep], false);
      eyeWiggleStep++;
    } else {
      drawEyes(0, false);
    }
  }
  if (eyeWiggleStep >= 5 && now - eyeLastWiggle >= 1600)
    eyeWiggleStep = 0;
  // trigger blink
  if (now >= eyeNextBlink) {
    eyeBlinking   = true;
    eyeBlinkStart = now;
    eyeBlinkPhase = 0;
    eyeWiggleStep = 0;
  }
}

// ═══════════════════════════════════════════════════
//  ANALOG CLOCK
// ═══════════════════════════════════════════════════
void drawHand(int cx, int cy, float angleDeg, int len, int thick) {
  float rad = (angleDeg - 90.0f) * 3.14159f / 180.0f;
  int ex = cx + (int)(len * cosf(rad));
  int ey = cy + (int)(len * sinf(rad));
  if (thick <= 1) {
    u8g2.drawLine(cx, cy, ex, ey);
  } else {
    // draw 3 parallel lines for thick hand
    float perp = rad + 3.14159f / 2.0f;
    for (int d = -1; d <= 1; d++) {
      int ox2 = (int)(d * cosf(perp));
      int oy2 = (int)(d * sinf(perp));
      u8g2.drawLine(cx+ox2, cy+oy2, ex+ox2, ey+oy2);
    }
  }
}

void drawAnalog() {
  int h, m, s; currentTime(h, m, s);

  u8g2.clearBuffer();
  u8g2.drawRFrame(0, 0, 128, 64, 6);

  // Clock circle
  int cx = 46, cy = 32, r = 27;
  u8g2.drawCircle(cx, cy, r, U8G2_DRAW_ALL);

  // Hour tick marks
  for (int i = 0; i < 12; i++) {
    float a = (i * 30.0f - 90.0f) * 3.14159f / 180.0f;
    int len = (i % 3 == 0) ? 4 : 2;
    int x1 = cx + (int)((r-1) * cosf(a));
    int y1 = cy + (int)((r-1) * sinf(a));
    int x2 = cx + (int)((r-1-len) * cosf(a));
    int y2 = cy + (int)((r-1-len) * sinf(a));
    u8g2.drawLine(x1, y1, x2, y2);
  }

  // Seconds hand (thin)
  float secAngle = s * 6.0f;
  drawHand(cx, cy, secAngle, r - 5, 1);

  // Minute hand (medium)
  float minAngle = m * 6.0f + s * 0.1f;
  drawHand(cx, cy, minAngle, r - 8, 2);

  // Hour hand (thick short)
  float hrAngle = (h % 12) * 30.0f + m * 0.5f;
  drawHand(cx, cy, hrAngle, r - 13, 2);

  // Center dot
  u8g2.drawDisc(cx, cy, 2, U8G2_DRAW_ALL);

  // Digital readout on right side
  char timeBuf[9];
  sprintf(timeBuf, "%02d:%02d", h, m);
  u8g2.setFont(u8g2_font_logisoso16_tn);
  u8g2.drawStr(82, 28, timeBuf);

  char secBuf[5];
  sprintf(secBuf, ":%02d", s);
  u8g2.setFont(u8g2_font_profont10_mr);
  u8g2.drawStr(95, 40, secBuf);

  char dd[10];
  sprintf(dd, "%s %02d", DAY_NAMES[clkDay], clkDate);
  u8g2.setFont(u8g2_font_profont10_mr);
  u8g2.drawStr(82, 54, dd);

  u8g2.sendBuffer();
}

// ═══════════════════════════════════════════════════
//  STOPWATCH
// ═══════════════════════════════════════════════════
enum SwState { SW_IDLE, SW_RUNNING, SW_PAUSED };
SwState swState = SW_IDLE;
unsigned long swElapsed = 0, swStartedAt = 0;
int sw_ms = 0, sw_sec = 0, sw_min = 0;
int arcAngle = 0;
unsigned long lastArcMs = 0;
bool swColonVis = true;
unsigned long swLastBlink = 0;

void arcDot(int cx, int cy, int r, int angle) {
  float rad = angle * 3.14159f / 180.0f;
  int px = cx + (int)(r * cosf(rad));
  int py = cy + (int)(r * sinf(rad));
  u8g2.drawBox(px-1, py-1, 3, 3);
}

void drawSwIdle() {
  u8g2.drawRFrame(0, 0, 128, 64, 6);
  u8g2.setFont(u8g2_font_profont10_mr);
  u8g2.drawStr(30, 13, "STOPWATCH");
  for (int x = 10; x < 118; x += 6) u8g2.drawBox(x, 18, 3, 1);
  u8g2.setFont(u8g2_font_logisoso28_tn);
  u8g2.drawStr(14, 50, "00:00");
  u8g2.setFont(u8g2_font_profont10_mr);
  u8g2.drawStr(16, 62, "dbl:start");
}

void drawSwRunning() {
  u8g2.drawRFrame(0, 0, 128, 64, 6);
  int cx = 64, cy = 32, r = 29;
  // Arc trail — dots get smaller as they trail behind
  for (int a = 0; a < 300; a += 15) {
    int aa = (arcAngle + a) % 360;
    float rad = aa * 3.14159f / 180.0f;
    int px = cx + (int)(r * cosf(rad));
    int py = cy + (int)(r * sinf(rad));
    int sz = (a < 30) ? 3 : (a < 90) ? 2 : 1;
    u8g2.drawBox(px - sz/2, py - sz/2, sz, sz);
  }
  arcDot(cx, cy, r, arcAngle);
  // MM:SS
  char buf[6]; sprintf(buf, "%02d:%02d", sw_min, sw_sec);
  u8g2.setFont(u8g2_font_logisoso28_tn);
  int tw = u8g2.getStrWidth(buf);
  u8g2.drawStr((128-tw)/2, 43, buf);
  char ms2[5]; sprintf(ms2, ".%02d", sw_ms);
  u8g2.setFont(u8g2_font_profont10_mr);
  u8g2.drawStr((128 - u8g2.getStrWidth(ms2))/2 + 28, 54, ms2);
  u8g2.drawStr(22, 62, "tap:pause");
}

void drawSwPaused() {
  u8g2.drawRFrame(0, 0, 128, 64, 6);
  u8g2.setFont(u8g2_font_profont10_mr);
  u8g2.drawStr(42, 13, "PAUSED");
  for (int x = 10; x < 118; x += 6) u8g2.drawBox(x, 17, 3, 1);
  if (swColonVis) {
    char buf[6]; sprintf(buf, "%02d:%02d", sw_min, sw_sec);
    u8g2.setFont(u8g2_font_logisoso28_tn);
    int tw = u8g2.getStrWidth(buf);
    u8g2.drawStr((128-tw)/2, 46, buf);
    char ms2[5]; sprintf(ms2, ".%02d", sw_ms);
    u8g2.setFont(u8g2_font_profont10_mr);
    u8g2.drawStr((128 - u8g2.getStrWidth(ms2))/2 + 28, 57, ms2);
  }
  u8g2.setFont(u8g2_font_profont10_mr);
  u8g2.drawStr(2, 62, "dbl:go  hold:rst");
}

void tickStopwatch() {
  unsigned long now = millis();
  if (swState == SW_RUNNING) {
    swElapsed = now - swStartedAt;
    sw_ms  = (swElapsed % 1000) / 10;
    sw_sec = (swElapsed / 1000) % 60;
    sw_min = (swElapsed / 60000) % 60;
  }
  if (swState == SW_RUNNING && now - lastArcMs >= 35) {
    lastArcMs = now;
    arcAngle = (arcAngle + 8) % 360;
  }
  if (now - swLastBlink >= 500) { swLastBlink = now; swColonVis = !swColonVis; }
  u8g2.clearBuffer();
  switch(swState) {
    case SW_IDLE:    drawSwIdle();    break;
    case SW_RUNNING: drawSwRunning(); break;
    case SW_PAUSED:  drawSwPaused();  break;
  }
  u8g2.sendBuffer();
}

// ═══════════════════════════════════════════════════
//  TERMINAL
// ═══════════════════════════════════════════════════
#define TERM_COLS 18
#define TERM_ROWS  4
bool    termActive = false;
String  termLines[TERM_ROWS];
uint8_t termRow = 0, termCol = 0;
unsigned long termCursorMs = 0;
bool    termCursorVis = true;

void termClear() {
  for (uint8_t i = 0; i < TERM_ROWS; i++) termLines[i] = "";
  termRow = 0; termCol = 0;
}

void termDraw() {
  u8g2.clearBuffer();
  // header bar
  u8g2.drawBox(0, 0, 128, 12);
  u8g2.setDrawColor(0);
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(3, 9, "clawd@mochi:~$");
  u8g2.setDrawColor(1);
  u8g2.drawHLine(0, 13, 128);

  u8g2.setFont(u8g2_font_5x7_tr);
  for (uint8_t r = 0; r < TERM_ROWS; r++) {
    int yy = 23 + r * 12;
    u8g2.drawStr(2, yy, termLines[r].c_str());
    // cursor on active row
    if (r == termRow && termCursorVis) {
      int cx2 = 2 + termCol * 6;
      u8g2.drawBox(cx2, yy - 7, 5, 8);
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

void tickTerminal() {
  unsigned long now = millis();
  if (now - termCursorMs >= 530) {
    termCursorMs = now;
    termCursorVis = !termCursorVis;
    termDraw();
  }
}

// ═══════════════════════════════════════════════════
//  HELPERS
// ═══════════════════════════════════════════════════
void flashInvert() {
  u8g2.setDrawColor(2);
  u8g2.drawBox(0, 0, 128, 64);
  u8g2.sendBuffer();
  delay(45);
  u8g2.setDrawColor(1);
}

void drawBootSplash() {
  u8g2.clearBuffer();
  // border
  u8g2.drawRFrame(0, 0, 128, 64, 6);
  // eyes icon
  int ex = 34, ey = 10, ew = 14, eh = 20, gap = 20;
  u8g2.drawRBox(ex, ey, ew, eh, 3);
  u8g2.drawRBox(ex + ew + gap, ey, ew, eh, 3);
  u8g2.setDrawColor(0);
  u8g2.drawBox(ex+2, ey+3, 3, 3);
  u8g2.drawBox(ex+ew+gap+2, ey+3, 3, 3);
  u8g2.setDrawColor(1);
  // text
  u8g2.setFont(u8g2_font_ncenB10_tr);
  u8g2.drawStr(28, 46, "Clawd Mochi");
  u8g2.setFont(u8g2_font_profont10_mr);
  u8g2.drawStr(44, 58, "v2.0");
  u8g2.sendBuffer();
}

// ═══════════════════════════════════════════════════
//  MODE SWITCHING
// ═══════════════════════════════════════════════════
void switchToMode(uint8_t m) {
  currentMode = m;
  termActive  = false;
  switch(m) {
    case MODE_EYES:
      eyeWiggleStep = 0;
      eyeBlinking   = false;
      eyeNextBlink  = millis() + 2500;
      eyeLastWiggle = millis();
      drawEyes(0, false);
      break;
    case MODE_ANALOG:
      drawAnalog();
      break;
    case MODE_SW:
      u8g2.clearBuffer();
      switch(swState) {
        case SW_IDLE:    drawSwIdle();    break;
        case SW_RUNNING: drawSwRunning(); break;
        case SW_PAUSED:  drawSwPaused();  break;
      }
      u8g2.sendBuffer();
      break;
    case MODE_TERMINAL:
      // show "CODE" splash briefly
      u8g2.clearBuffer();
      u8g2.drawHLine(0, 0, 128);
      u8g2.drawHLine(0, 63, 128);
      u8g2.setFont(u8g2_font_ncenB14_tr);
      u8g2.drawStr(20, 32, "Claude");
      u8g2.setFont(u8g2_font_ncenB10_tr);
      u8g2.drawStr(38, 50, "Code");
      u8g2.sendBuffer();
      delay(700);
      termActive = true;
      termClear();
      termDraw();
      break;
  }
}

void nextMode() {
  flashInvert();
  if (currentMode == MODE_SW) {
    switchToMode(MODE_EYES);
  } else {
    switchToMode((currentMode + 1) % MODE_COUNT);
  }
}

// ═══════════════════════════════════════════════════
//  TOUCH HANDLER
// ═══════════════════════════════════════════════════
void handleTouch() {
  bool touched = (digitalRead(TOUCH_PIN) == HIGH);
  unsigned long now = millis();

  // press start
  if (touched && !lastTouched) {
    touchDownAt = now;
    longFired   = false;
  }

  // long press check (while held)
  if (touched && lastTouched && !longFired) {
    if (now - touchDownAt >= LONG_MS) {
      longFired = true;
      if (currentMode == MODE_SW) {
        // long → reset stopwatch
        swState = SW_IDLE;
        swElapsed = 0;
        sw_ms = sw_sec = sw_min = 0;
        arcAngle = 0;
        flashInvert();
      } else {
        nextMode();
      }
    }
  }

  // release without long fire
  if (!touched && lastTouched && !longFired) {
    if (now - touchDownAt < LONG_MS) {
      if (currentMode == MODE_SW) {
        if (waitDoubleTap && (now - lastReleaseAt < DOUBLE_MS)) {
          // double tap in SW → start/resume
          waitDoubleTap = false;
          if (swState == SW_IDLE || swState == SW_PAUSED) {
            swState     = SW_RUNNING;
            swStartedAt = millis() - swElapsed;
            flashInvert();
          }
        } else {
          waitDoubleTap = true;
          lastReleaseAt = now;
        }
      } else {
        // non-SW: single tap queued (double tap just advances mode again)
        if (waitDoubleTap && (now - lastReleaseAt < DOUBLE_MS)) {
          waitDoubleTap = false;
          nextMode();
        } else {
          waitDoubleTap = true;
          lastReleaseAt = now;
        }
      }
    }
  }

  // single-tap timeout fires
  if (waitDoubleTap && (now - lastReleaseAt > DOUBLE_MS)) {
    waitDoubleTap = false;
    if (currentMode == MODE_SW) {
      if (swState == SW_RUNNING) {
        // single tap while running → pause
        swState   = SW_PAUSED;
        swElapsed = millis() - swStartedAt;
      } else {
        // single tap while idle or paused → back to eyes
        switchToMode(MODE_EYES);
      }
    } else {
      nextMode();
    }
  }

  lastTouched = touched;
}

// ═══════════════════════════════════════════════════
//  WEB UI
// ═══════════════════════════════════════════════════
const char INDEX_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>Clawd Mochi</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}
body{background:#131316;font-family:'Courier New',monospace;color:#ddd9d0;
  display:flex;flex-direction:column;align-items:center;padding:18px 14px 56px;gap:14px;min-height:100vh}
.logo{font-size:28px;letter-spacing:4px;color:#cf7040;font-weight:bold;text-align:center;margin-top:4px}
.sub{font-size:9px;color:#4a4640;letter-spacing:3px;text-align:center}
.sec{width:100%;max-width:390px;font-size:9px;color:#6a6258;letter-spacing:2px;font-weight:bold;padding:0 2px;text-transform:uppercase}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;width:100%;max-width:390px}
.grid5{display:grid;grid-template-columns:repeat(5,1fr);gap:6px;width:100%;max-width:390px}
.vbtn{background:#1e1d21;border:1.5px solid #2e2c32;border-radius:12px;color:#bbb8b0;
  font-family:'Courier New',monospace;padding:13px 4px 9px;cursor:pointer;text-align:center;transition:all .12s}
.vbtn:active:not(:disabled){transform:scale(.93)}
.vbtn .ic{font-size:16px;display:block;margin-bottom:4px;color:#cf7040}
.vbtn .nm{font-size:11px;font-weight:bold;color:#ddd9d0}
.vbtn .ht{font-size:8px;color:#6a6258;margin-top:2px}
.vbtn.active{border-color:#cf7040;background:#201208}
.row{display:flex;gap:8px;width:100%;max-width:390px}
.swb{flex:1;background:#1e1d21;border:1.5px solid #2e2c32;border-radius:10px;color:#aaa8a0;
  font-family:'Courier New',monospace;font-size:11px;font-weight:bold;
  padding:11px 4px;cursor:pointer;text-align:center;transition:all .12s}
.swb:active{transform:scale(.93)}
.swb.go{border-color:#28b868;color:#28b868}
.swb.pause{border-color:#cf7040;color:#cf7040}
.swb.rst{border-color:#445;color:#778}
.clkrow{width:100%;max-width:390px;display:flex;gap:6px}
.ci{display:flex;flex-direction:column;gap:3px;flex:1}
.cl{font-size:9px;color:#6a6258;letter-spacing:1px;font-weight:bold;text-align:center}
.cn{background:#1e1d21;border:1.5px solid #2e2c32;border-radius:8px;
  color:#ddd9d0;font-family:'Courier New',monospace;font-size:13px;
  padding:7px 4px;width:100%;outline:none;text-align:center}
.twrap{display:none;flex-direction:column;gap:8px;width:100%;max-width:390px}
.twrap.open{display:flex}
.thdr{display:flex;justify-content:space-between;align-items:center}
.tttl{font-size:11px;color:#28b868;letter-spacing:1px;font-weight:bold}
.tx{background:#0c1e10;border:1.5px solid #1a4820;border-radius:8px;color:#28b868;
  font-family:'Courier New',monospace;font-size:11px;padding:7px 12px;cursor:pointer}
.trow{display:flex;gap:6px}
.tin{flex:1;background:#0c0e18;border:1.5px solid #1a2020;border-radius:9px;
  color:#38e070;font-family:'Courier New',monospace;font-size:15px;padding:10px;outline:none}
.tgo{background:#166040;border:none;border-radius:9px;color:#fff;
  font-family:'Courier New',monospace;font-size:20px;font-weight:bold;
  padding:10px 15px;cursor:pointer;min-width:48px}
.toast{position:fixed;bottom:16px;left:50%;transform:translateX(-50%);
  background:#1e1d21;border:1.5px solid #2e2c32;border-radius:9px;
  font-size:11px;color:#ddd9d0;padding:7px 16px;opacity:0;
  transition:opacity .15s;pointer-events:none;z-index:99}
.toast.show{opacity:1}
.busy{width:100%;max-width:390px;height:2px;background:#1e1d21;border-radius:1px;overflow:hidden;opacity:0;transition:opacity .2s}
.busy.show{opacity:1}
.busy-i{height:100%;width:30%;background:#cf7040;animation:sl 1s linear infinite}
@keyframes sl{0%{margin-left:-30%}100%{margin-left:100%}}
</style></head><body>
<div class="logo">[ @ @ ]</div>
<div class="sub">CLAWD &middot; MOCHI &middot; V2</div>
<div class="busy" id="busy"><div class="busy-i"></div></div>
<div class="sec">// mode</div>
<div class="grid">
  <button class="vbtn active" data-m="0" onclick="setMode(0)"><span class="ic">&#9632;&#9632;</span><span class="nm">Eyes</span><span class="ht">blink+wiggle</span></button>
  <button class="vbtn" data-m="1" onclick="setMode(1)"><span class="ic">&#9711;</span><span class="nm">Analog</span><span class="ht">sweep hands</span></button>
  <button class="vbtn" data-m="2" onclick="setMode(2)"><span class="ic">&#9654;</span><span class="nm">Stopwatch</span><span class="ht">start/pause</span></button>
  <button class="vbtn" data-m="3" onclick="setMode(3)" style="grid-column:span 3"><span class="ic">{ }</span><span class="nm">Terminal</span><span class="ht">type on screen</span></button>
</div>
<div class="sec">// stopwatch</div>
<div class="row">
  <button class="swb go"    onclick="swCmd('start')">&#9654; start</button>
  <button class="swb pause" onclick="swCmd('pause')">&#9646;&#9646; pause</button>
  <button class="swb rst"   onclick="swCmd('reset')">&#9632; reset</button>
</div>
<div class="sec">// set time</div>
<div class="clkrow">
  <div class="ci"><span class="cl">HR</span><input class="cn" id="cHr"  type="number" min="0" max="23" value="10"></div>
  <div class="ci"><span class="cl">MIN</span><input class="cn" id="cMin" type="number" min="0" max="59" value="0"></div>
  <div class="ci"><span class="cl">SEC</span><input class="cn" id="cSec" type="number" min="0" max="59" value="0"></div>
  <div class="ci"><span class="cl">DAY</span>
    <select class="cn" id="cDay">
      <option value="0">Sun</option><option value="1">Mon</option>
      <option value="2">Tue</option><option value="3" selected>Wed</option>
      <option value="4">Thu</option><option value="5">Fri</option>
      <option value="6">Sat</option>
    </select>
  </div>
  <div class="ci"><span class="cl">DATE</span><input class="cn" id="cDate" type="number" min="1" max="31" value="7"></div>
</div>
<button class="swb go" style="width:100%;max-width:390px;margin-top:2px" onclick="setTime()">&#9654; set time</button>
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
let activeMode=0,termOpen=false,tt;
function toast(msg,ok=true){
  const el=document.getElementById('toast');
  el.textContent=msg;el.style.borderColor=ok?'#28b868':'#cf7040';
  el.classList.add('show');clearTimeout(tt);
  tt=setTimeout(()=>el.classList.remove('show'),1300);
}
async function req(path){
  try{const r=await fetch(path);return r.ok;}
  catch{toast('no connection',false);return false;}
}
const modeKeys=['w','a','t','d'];
const modeLabels=['eyes','analog','stopwatch','terminal'];
async function setMode(m){
  if(!await req('/cmd?k='+modeKeys[m]))return;
  activeMode=m;
  document.querySelectorAll('.vbtn').forEach(b=>b.classList.toggle('active',parseInt(b.dataset.m)===m));
  const tw=document.getElementById('twrap');
  if(m===3){tw.classList.add('open');document.getElementById('tin').focus();}
  else tw.classList.remove('open');
  toast(modeLabels[m]);
}
async function swCmd(a){await req('/sw?a='+a);toast(a);}
async function setTime(){
  const h=document.getElementById('cHr').value,
        m=document.getElementById('cMin').value,
        s=document.getElementById('cSec').value,
        dy=document.getElementById('cDay').value,
        dt=document.getElementById('cDate').value;
  if(await req('/settime?h='+h+'&m='+m+'&s='+s+'&dy='+dy+'&dt='+dt))toast('time set!');
}
const tin=document.getElementById('tin');
let lastVal='';
tin.addEventListener('input',async()=>{
  const cur=tin.value,prev=lastVal;
  if(cur.length>prev.length)await req('/char?c='+encodeURIComponent(cur[cur.length-1]));
  else if(cur.length<prev.length)await req('/char?c=%08');
  lastVal=cur;
});
async function termEnter(){await req('/char?c=%0A');tin.value='';lastVal='';tin.focus();}
tin.addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();termEnter();}});
async function closeTerm(){
  document.getElementById('twrap').classList.remove('open');
  await req('/cmd?k=q');toast('terminal closed');
}
</script></body></html>
)rawhtml";

// ── Web Routes ────────────────────────────────────────────────
void routeRoot() {
  server.sendHeader("Cache-Control","no-store");
  server.send_P(200,"text/html",INDEX_HTML);
}

void routeCmd() {
  if (!server.hasArg("k")) { server.send(400,"text/plain","bad"); return; }
  char c = server.arg("k")[0];
  server.send(200,"application/json","{\"ok\":1}");
  if (termActive && c != 'q') return;
  switch(c) {
    case 'w': switchToMode(MODE_EYES);     break;
    case 'a': switchToMode(MODE_ANALOG);   break;
    case 't': switchToMode(MODE_SW);       break;
    case 'd': switchToMode(MODE_TERMINAL); break;
    case 'q': termActive = false; switchToMode(MODE_EYES); break;
  }
}

void routeChar() {
  if (!termActive) { server.send(200,"application/json","{\"ok\":1}"); return; }
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
    if (swState == SW_RUNNING) { swState = SW_PAUSED; swElapsed = millis() - swStartedAt; }
  } else if (a == "reset") {
    swState = SW_IDLE; swElapsed = 0; sw_ms = sw_sec = sw_min = 0; arcAngle = 0;
  }
  server.send(200,"application/json","{\"ok\":1}");
}

void routeSetTime() {
  if (server.hasArg("h"))  clkHr   = constrain(server.arg("h").toInt(),  0, 23);
  if (server.hasArg("m"))  clkMin  = constrain(server.arg("m").toInt(),  0, 59);
  if (server.hasArg("s"))  clkSec  = constrain(server.arg("s").toInt(),  0, 59);
  if (server.hasArg("dy")) clkDay  = constrain(server.arg("dy").toInt(), 0,  6);
  if (server.hasArg("dt")) clkDate = constrain(server.arg("dt").toInt(), 1, 31);
  clockBaseMs = millis();
  server.send(200,"application/json","{\"ok\":1}");
}

void routeNotFound() { server.send(404,"text/plain","not found"); }

// ═══════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  pinMode(TOUCH_PIN, INPUT_PULLDOWN);
  Wire.begin(OLED_SDA, OLED_SCL);
  u8g2.begin();
  u8g2.setContrast(255);
  u8g2.setBusClock(400000);

  drawBootSplash();
  delay(1600);

  // WiFi AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  // WiFi info screen
  u8g2.clearBuffer();
  u8g2.drawRFrame(0, 0, 128, 64, 5);
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(5,  12, "WiFi: ClaWD-Mochi");
  u8g2.drawStr(5,  23, "pw: clawd1234");
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(5,  40, "192.168.4.1");
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(5,  54, "touch = next mode");
  u8g2.sendBuffer();
  delay(2200);

  server.on("/",        HTTP_GET, routeRoot);
  server.on("/cmd",     HTTP_GET, routeCmd);
  server.on("/char",    HTTP_GET, routeChar);
  server.on("/sw",      HTTP_GET, routeSw);
  server.on("/settime", HTTP_GET, routeSetTime);
  server.onNotFound(routeNotFound);
  server.begin();

  clockBaseMs = millis();
  switchToMode(MODE_EYES);
}

// ═══════════════════════════════════════════════════
//  LOOP
// ═══════════════════════════════════════════════════
void loop() {
  server.handleClient();
  handleTouch();

  switch(currentMode) {
    case MODE_EYES:     tickEyes();      break;
    case MODE_ANALOG:   drawAnalog();    break;
    case MODE_SW:       tickStopwatch(); break;
    case MODE_TERMINAL: tickTerminal();  break;
  }
}
