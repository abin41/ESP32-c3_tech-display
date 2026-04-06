#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

// ========== PIN DEFINITIONS ==========
#define OLED_SDA   6
#define OLED_SCL   7
#define TOUCH_PIN  3

// ========== DISPLAY ==========
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);

// ========== STOPWATCH STATE ==========
enum SwState { SW_IDLE, SW_RUNNING, SW_PAUSED };
SwState swState = SW_IDLE;

unsigned long swElapsed   = 0;      // total elapsed ms
unsigned long swStartedAt = 0;      // millis() when last started

int sw_ms  = 0;
int sw_sec = 0;
int sw_min = 0;

// ========== TOUCH DETECTION ==========
bool lastTouchState    = false;
unsigned long touchDownAt  = 0;
unsigned long lastReleaseAt = 0;
bool waitingDoubleTap  = false;
bool longFired         = false;

const unsigned long LONG_PRESS_MS   = 700;
const unsigned long DOUBLE_TAP_MS   = 350;

// ========== ANIMATION ==========
unsigned long lastFrame = 0;
int  arcAngle     = 0;    // spinning arc for IDLE/RUNNING
bool colonVis     = true;
unsigned long lastBlink = 0;

// ========== PROTOTYPES ==========
void handleTouch();
void updateStopwatch();
void updateDisplay();
void drawIdle();
void drawRunning();
void drawPaused();
void drawReset();
void arcDot(int cx, int cy, int r, int angleDeg);
void flashInvert();

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  pinMode(TOUCH_PIN, INPUT_PULLDOWN);
  u8g2.begin();
  u8g2.setContrast(255);
  u8g2.setBusClock(400000);
  updateDisplay();
}

// ========== LOOP ==========
void loop() {
  handleTouch();
  updateStopwatch();

  // Spinning arc animation frame
  if (millis() - lastFrame >= 40) {
    lastFrame = millis();
    if (swState == SW_RUNNING) arcAngle = (arcAngle + 6) % 360;
    updateDisplay();
  }

  // Colon blink when paused
  if (millis() - lastBlink >= 500) {
    lastBlink = millis();
    colonVis = !colonVis;
  }
}

// ========== TOUCH HANDLER ==========
void handleTouch() {
  bool touched = (digitalRead(TOUCH_PIN) == HIGH);
  unsigned long now = millis();

  // --- Finger just pressed ---
  if (touched && !lastTouchState) {
    touchDownAt = now;
    longFired   = false;
  }

  // --- Held down: check long press ---
  if (touched && lastTouchState && !longFired) {
    if (now - touchDownAt >= LONG_PRESS_MS) {
      longFired = true;
      // LONG PRESS → RESET
      swState   = SW_IDLE;
      swElapsed = 0;
      sw_ms = sw_sec = sw_min = 0;
      arcAngle = 0;
      flashInvert();
      Serial.println("RESET");
    }
  }

  // --- Finger released ---
  if (!touched && lastTouchState && !longFired) {
    unsigned long pressDur = now - touchDownAt;

    if (pressDur < LONG_PRESS_MS) {
      if (waitingDoubleTap && (now - lastReleaseAt < DOUBLE_TAP_MS)) {
        // DOUBLE TAP → START
        waitingDoubleTap = false;
        if (swState == SW_IDLE || swState == SW_PAUSED) {
          swState     = SW_RUNNING;
          swStartedAt = millis() - swElapsed;
          flashInvert();
          Serial.println("START");
        }
      } else {
        waitingDoubleTap = true;
        lastReleaseAt    = now;
      }
    }
  }

  // --- Single tap timeout → PAUSE or nothing ---
  if (waitingDoubleTap && (now - lastReleaseAt > DOUBLE_TAP_MS)) {
    waitingDoubleTap = false;
    // SINGLE TAP → PAUSE (only if running)
    if (swState == SW_RUNNING) {
      swState   = SW_PAUSED;
      swElapsed = millis() - swStartedAt;
      Serial.println("PAUSE");
    }
  }

  lastTouchState = touched;
}

// ========== STOPWATCH LOGIC ==========
void updateStopwatch() {
  if (swState != SW_RUNNING) return;
  swElapsed = millis() - swStartedAt;
  sw_ms  = (swElapsed % 1000) / 10;   // centiseconds 00-99
  sw_sec = (swElapsed / 1000) % 60;
  sw_min = (swElapsed / 60000) % 60;
}

// ========== FLASH INVERT ==========
void flashInvert() {
  u8g2.setDrawColor(2);
  u8g2.drawBox(0, 0, 128, 64);
  u8g2.sendBuffer();
  delay(50);
  u8g2.setDrawColor(1);
}

// ========== DRAW ARC DOT (spinning dot on circle) ==========
void arcDot(int cx, int cy, int r, int angleDeg) {
  float rad = angleDeg * 3.14159f / 180.0f;
  int x = cx + (int)(r * cos(rad));
  int y = cy + (int)(r * sin(rad));
  u8g2.drawBox(x - 1, y - 1, 3, 3);
}

// ========== IDLE SCREEN ==========
void drawIdle() {
  // Border
  u8g2.drawRFrame(0, 0, 128, 64, 6);

  // "STOPWATCH" title
  u8g2.setFont(u8g2_font_profont10_mr);
  u8g2.drawStr(34, 13, "STOPWATCH");

  // Dashed center divider
  for (int x = 10; x < 118; x += 6) u8g2.drawBox(x, 18, 3, 1);

  // Big zeroed time
  u8g2.setFont(u8g2_font_logisoso28_tn);
  u8g2.drawStr(14, 50, "00:00");

  // Hint
  u8g2.setFont(u8g2_font_profont10_mr);
  u8g2.drawStr(28, 61, " start");
}

// ========== RUNNING SCREEN ==========
void drawRunning() {
  u8g2.drawRFrame(0, 0, 128, 64, 6);

  // Spinning arc ring (center 64,32, radius 28)
  int cx = 64, cy = 32, r = 29;
  // Draw partial arc (every 20deg, 16 dots = ~320deg sweep)
  for (int a = 0; a < 300; a += 20) {
    int aa = (arcAngle + a) % 360;
    float rad = aa * 3.14159f / 180.0f;
    int px = cx + (int)(r * cos(rad));
    int py = cy + (int)(r * sin(rad));
    // Fade: dots further behind are smaller
    int size = (a < 60) ? 2 : 1;
    u8g2.drawBox(px - size/2, py - size/2, size, size);
  }

  // Leading bright dot
  arcDot(cx, cy, r, arcAngle);

  // MM:SS large center
  char buf[6];
  sprintf(buf, "%02d:%02d", sw_min, sw_sec);
  u8g2.setFont(u8g2_font_logisoso28_tn);
  int tw = u8g2.getStrWidth(buf);
  u8g2.drawStr((128 - tw) / 2, 43, buf);

  // Centiseconds small below
  char ms[3];
  sprintf(ms, ".%02d", sw_ms);
  u8g2.setFont(u8g2_font_profont10_mr);
  int mw = u8g2.getStrWidth(ms);
  u8g2.drawStr((128 - mw) / 2 + 28, 54, ms);

  // "tap to pause" hint bottom
  u8g2.drawStr(22, 62, "tap to pause");
}

// ========== PAUSED SCREEN ==========
void drawPaused() {
  u8g2.drawRFrame(0, 0, 128, 64, 6);

  // "PAUSED" label top
  u8g2.setFont(u8g2_font_profont10_mr);
  u8g2.drawStr(46, 13, "PAUSED");

  // Dashed divider
  for (int x = 10; x < 118; x += 6) u8g2.drawBox(x, 17, 3, 1);

  // Blinking time
  if (colonVis) {
    char buf[6];
    sprintf(buf, "%02d:%02d", sw_min, sw_sec);
    u8g2.setFont(u8g2_font_logisoso28_tn);
    int tw = u8g2.getStrWidth(buf);
    u8g2.drawStr((128 - tw) / 2, 46, buf);

    char ms[3];
    sprintf(ms, ".%02d", sw_ms);
    u8g2.setFont(u8g2_font_profont10_mr);
    int mw = u8g2.getStrWidth(ms);
    u8g2.drawStr((128 - mw) / 2 + 28, 57, ms);
  }

  // Hints
  u8g2.setFont(u8g2_font_profont10_mr);
  u8g2.drawStr(6, 62, "  hold:reset");
}

// ========== UPDATE DISPLAY ==========
void updateDisplay() {
  u8g2.clearBuffer();
  switch (swState) {
    case SW_IDLE:    drawIdle();    break;
    case SW_RUNNING: drawRunning(); break;
    case SW_PAUSED:  drawPaused();  break;
  }
  u8g2.sendBuffer();
}
