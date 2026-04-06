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
int currentHours = 10;      // Change to your current hour (24h format)
bool timeChanged = true;

// ========== POWER & EFFECTS ==========
const unsigned long DISPLAY_TIMEOUT_MS = 6000;
unsigned long lastTouchTime = 0;
bool displayOn = true;
bool colonVisible = true;
unsigned long lastBlink = 0;

// ========== FUNCTION PROTOTYPES ==========
void updateTime();
void updateDisplay();
void handleTouch();
void drawBorder();
void drawClockFace();

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  Serial.println("\nSmartwatch Starting...");

  pinMode(TOUCH_PIN, INPUT_PULLDOWN);

  u8g2.begin();
  u8g2.setContrast(255);
  u8g2.setBusClock(400000);

  lastTouchTime = millis();
  displayOn = true;
  updateDisplay();

  Serial.println("Ready – touch to wake, auto-off after 6s");
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
      Serial.println("Display ON by touch");
    } else {
      // Flash effect
      u8g2.setDrawColor(2);
      u8g2.drawBox(0, 0, 128, 64);
      u8g2.sendBuffer();
      delay(50);
      u8g2.setDrawColor(1);
      updateDisplay();
    }
    lastTouchTime = millis();
  }

  if (displayOn && (millis() - lastTouchTime > DISPLAY_TIMEOUT_MS)) {
    displayOn = false;
    u8g2.setPowerSave(1);
    Serial.println("Display OFF");
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
        if (currentHours >= 24) currentHours = 0;
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
  u8g2.drawRFrame(0, 0, 128, 64, 8);
}

// ========== CLOCK FACE WITH PERFECT FIT ==========
void drawClockFace() {
  char hourStr[3];
  char minuteStr[3];
  sprintf(hourStr, "%02d", currentHours);
  sprintf(minuteStr, "%02d", currentMinutes);

  // Use 28px Logisoso font (clean digital, fits well)
  u8g2.setFont(u8g2_font_logisoso28_tn);
  
  int hWidth = u8g2.getStrWidth(hourStr);
  int mWidth = u8g2.getStrWidth(minuteStr);
  
  // Colon position (center of the display)
  int colonX = 64;
  
  // Hours placed left of colon, minutes right
  int hourX = colonX - hWidth - 6;   // 6px gap from colon
  int minuteX = colonX + 6;
  
  // Y position: 40 (leaves room for border and seconds)
  int yMain = 42;
  
  u8g2.drawStr(hourX, yMain, hourStr);
  u8g2.drawStr(minuteX, yMain, minuteStr);
  
  // Blinking colon (use same font, but draw as single character)
  if (colonVisible) {
    u8g2.setFont(u8g2_font_logisoso28_tn);
    u8g2.drawStr(colonX - 8, yMain, ":"); // Adjust X to center colon
  }
  
  // Seconds at bottom (small, neat)
  char secStr[3];
  sprintf(secStr, "%02d", currentSeconds);
  u8g2.setFont(u8g2_font_helvB10_tn);   // 10px Helvetica bold
  int secWidth = u8g2.getStrWidth(secStr);
  u8g2.drawStr((128 - secWidth) / 2, 58, secStr);
}

// ========== UPDATE DISPLAY ==========
void updateDisplay() {
  if (!displayOn) return;
  
  u8g2.clearBuffer();
  drawBorder();
  drawClockFace();
  u8g2.sendBuffer();
}
