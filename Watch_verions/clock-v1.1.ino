#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

// ========== PIN DEFINITIONS ==========
#define OLED_SDA   6
#define OLED_SCL   7
#define TOUCH_PIN  3

// ========== DISPLAY OBJECT (1.3" SH1106) ==========
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);

// ========== TIME VARIABLES ==========
unsigned long lastMillis = 0;
int currentSeconds = 0;
int currentMinutes = 0;
int currentHours = 10;
int currentDay   = 1;   // 0=Sun..6=Sat
int currentDate  = 6;   // day of month
bool timeChanged = true;

// ========== POWER & EFFECTS ==========
const unsigned long DISPLAY_TIMEOUT_MS = 6000;
unsigned long lastTouchTime = 0;
bool displayOn  = true;
bool colonVisible = true;
unsigned long lastBlink = 0;

const char* DAY_NAMES[] = { "SUN","MON","TUE","WED","THU","FRI","SAT" };

// ========== FUNCTION PROTOTYPES ==========
void updateTime();
void updateDisplay();
void handleTouch();
void drawBorder();
void drawClockFace();

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  pinMode(TOUCH_PIN, INPUT_PULLDOWN);
  u8g2.begin();
  u8g2.setContrast(255);
  u8g2.setBusClock(400000);
  lastTouchTime = millis();
  displayOn = true;
  updateDisplay();
}

// ========== MAIN LOOP ==========
void loop() {
  handleTouch();
  updateTime();
  if (millis() - lastBlink >= 500) {
    lastBlink = millis();
    colonVisible = !colonVisible;
    if (displayOn) updateDisplay();
  }
  delay(50);
}

// ========== TOUCH HANDLER ==========
void handleTouch() {
  bool isTouched = (digitalRead(TOUCH_PIN) == HIGH);
  if (isTouched) {
    if (!displayOn) {
      displayOn = true;
      u8g2.setPowerSave(0);
      updateDisplay();
    } else {
      // Quick invert flash
      u8g2.sendBuffer();
      u8g2.setDrawColor(2);
      u8g2.drawBox(2, 2, 124, 60);
      u8g2.sendBuffer();
      delay(40);
      u8g2.setDrawColor(1);
      updateDisplay();
    }
    lastTouchTime = millis();
  }
  if (displayOn && (millis() - lastTouchTime > DISPLAY_TIMEOUT_MS)) {
    displayOn = false;
    u8g2.setPowerSave(1);
  }
}

// ========== TIME UPDATE ==========
void updateTime() {
  unsigned long now = millis();
  if (now - lastMillis >= 1000) {
    lastMillis = now;
    currentSeconds++;
    timeChanged = true;
    if (currentSeconds >= 60) {
      currentSeconds = 0;
      currentMinutes++;
      if (currentMinutes >= 60) {
        currentMinutes = 0;
        currentHours++;
        if (currentHours >= 24) {
          currentHours = 0;
          currentDate++;
          currentDay = (currentDay + 1) % 7;
        }
      }
    }
  }
  if (timeChanged && displayOn) {
    updateDisplay();
    timeChanged = false;
  }
}

// ========== ROUNDED BORDER ==========
void drawBorder() {
  u8g2.drawRFrame(0, 0, 128, 64, 6);
}

// ========== CLOCK FACE ==========
void drawClockFace() {
  char hourStr[3], minuteStr[3], secStr[3];
  sprintf(hourStr,   "%02d", currentHours);
  sprintf(minuteStr, "%02d", currentMinutes);
  sprintf(secStr,    "%02d", currentSeconds);

  // --- Day + Date top-right ---
  char dayDateStr[10];
  sprintf(dayDateStr, "%s %02d", DAY_NAMES[currentDay], currentDate);
  u8g2.setFont(u8g2_font_profont10_mr);
  int ddWidth = u8g2.getStrWidth(dayDateStr);
  u8g2.drawStr(124 - ddWidth, 12, dayDateStr);

  // --- Main HH:MM centered ---
  u8g2.setFont(u8g2_font_logisoso28_tn);
  int hWidth = u8g2.getStrWidth(hourStr);
  int mWidth = u8g2.getStrWidth(minuteStr);
  int colonWidth = u8g2.getStrWidth(":");

  // Total width of "HH:MM"
  int totalW = hWidth + colonWidth + mWidth;
  int startX = (128 - totalW) / 2;
  int yMain  = 46;

  u8g2.drawStr(startX, yMain, hourStr);

  if (colonVisible) {
    u8g2.drawStr(startX + hWidth, yMain, ":");
  }

  u8g2.drawStr(startX + hWidth + colonWidth, yMain, minuteStr);

  // --- Seconds progress bar (bottom) ---
  // Label "SEC" left, value right, bar in middle
  u8g2.setFont(u8g2_font_profont10_mr);
  int secLabelX = 8;
  int secValWidth = u8g2.getStrWidth(secStr);

 

  // Progress bar between label and value
  int barX = 26;
  int barW = 90;
  int barY = 56;
  int barH = 3;
  u8g2.drawRFrame(barX, barY, barW, barH, 1);      // outline
  int filled = (currentSeconds * barW) / 59;
  if (filled > 0) u8g2.drawBox(barX, barY, filled, barH);  // fill
}

// ========== UPDATE DISPLAY ==========
void updateDisplay() {
  if (!displayOn) return;
  u8g2.clearBuffer();
  drawBorder();
  drawClockFace();
  u8g2.sendBuffer();
}
