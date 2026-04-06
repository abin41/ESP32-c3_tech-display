/*
 * ================================================================
 * SMARTWATCH FIRMWARE v3.2  —  TTP223 TOUCH FIX
 * Hardware : ESP32-C3 Super Mini
 * SH1106 1.3" OLED  (I2C: SDA=GPIO6  SCL=GPIO7)
 * TTP223 Touch Sensor on GPIO3
 *
 * HOW TOUCH WORKS:
 * - Single tap  → cycle to next face
 * - Double tap  → jump back to Clock from any face
 * ================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <U8g2lib.h>
#include <time.h>

// ================================================================
//  1.  WIFI
// ================================================================
const char* ssid     = "afeez";
const char* password = "afeeZ@123";

// ================================================================
//  2.  TIME  (India Standard Time = UTC +5:30)
// ================================================================
const char* ntpServer     = "pool.ntp.org";
const long  gmtOffset_sec = 19800;
const int   dstOffset_sec = 0;

// ================================================================
//  3.  PINS
// ================================================================
#define TOUCH_PIN  3   // TTP223 I/O Pin → GPIO3
#define SDA_PIN    6
#define SCL_PIN    7

// ================================================================
//  4.  DISPLAY
// ================================================================
U8G2_SH1106_128X64_NONAME_F_SW_I2C
  u8g2(U8G2_R0, SCL_PIN, SDA_PIN, U8X8_PIN_NONE);

// ================================================================
//  5.  SERVERS
// ================================================================
WebServer        httpServer(80);
WebSocketsServer wsServer(81);

// ================================================================
//  6.  WATCH FACES
// ================================================================
enum WatchFace { FACE_CLOCK=0, FACE_STOPWATCH, FACE_TIMER,
                 FACE_MESSAGE, FACE_COUNT };
WatchFace currentFace = FACE_CLOCK;

// ================================================================
//  7.  STATE VARIABLES
// ================================================================
unsigned long lastActivity   = 0;
const unsigned long SLEEP_MS = 20000; // screen off after 20 s

bool   use12h    = false;
String customMsg = "";

// Stopwatch
bool          swRunning = false;
unsigned long swStartMs = 0;
unsigned long swSavedMs = 0;

// Countdown timer 
unsigned long cdDuration = 60000;
unsigned long cdStartMs  = 0;
bool          cdRunning  = false;
bool          cdDone     = false;

// Touch
bool          touchWasOn        = false;
unsigned long touchRisingTime   = 0;
unsigned long lastTapTime       = 0;
bool          pendingSingle     = false;
const unsigned long DEBOUNCE_MS = 50;
const unsigned long DBL_WIN_MS  = 350;

// Home banner
bool          showBanner  = false;
unsigned long bannerStart = 0;
const unsigned long BANNER_MS = 800;

// ================================================================
//  8.  WEB PAGE
// ================================================================
const char PAGE[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Watch Control</title>
<style>
:root{--bg:#0d0d14;--card:#13131f;--bd:#252535;--cy:#00e5ff;--rd:#ff4060;--gr:#39ff7a;--tx:#e0e0f0;--mt:#44445a}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--tx);font-family:system-ui,sans-serif;padding:16px 12px 60px}
h1{text-align:center;font-family:monospace;letter-spacing:3px;color:var(--cy);margin:14px 0 2px;font-size:19px}
.sub{text-align:center;color:var(--mt);font-size:12px;margin-bottom:16px}
.pill{display:flex;align-items:center;justify-content:center;gap:8px;background:var(--card);border:1px solid var(--bd);border-radius:999px;padding:7px 18px;font-size:13px;font-family:monospace;margin-bottom:18px}
.dot{width:9px;height:9px;border-radius:50%;background:var(--mt);transition:.3s}
.dot.on{background:var(--gr);box-shadow:0 0 8px var(--gr)}
.card{background:var(--card);border:1px solid var(--bd);border-radius:14px;padding:16px;margin-bottom:14px}
.lbl{font-size:11px;font-family:monospace;color:var(--mt);letter-spacing:2px;text-transform:uppercase;margin-bottom:10px}
input{width:100%;padding:11px 12px;font-size:15px;background:#0a0a12;border:1px solid var(--bd);border-radius:8px;color:var(--tx);outline:none;margin-bottom:8px}
input:focus{border-color:var(--cy)}
.btn{display:block;width:100%;padding:12px;font-size:14px;font-weight:700;border:none;border-radius:8px;cursor:pointer;margin-bottom:8px;transition:opacity .15s,transform .1s}
.btn:active{opacity:.75;transform:scale(.98)}
.btn:last-child{margin-bottom:0}
.cy{background:var(--cy);color:#000}
.rd{background:var(--rd);color:#fff}
.gr{background:var(--gr);color:#000}
.gh{background:transparent;color:var(--tx);border:1px solid var(--bd)}
.row{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.chips{display:flex;flex-wrap:wrap;gap:8px;margin-top:2px}
.chip{padding:7px 14px;border-radius:999px;font-size:12px;font-family:monospace;background:#1a1a2a;border:1px solid var(--bd);color:var(--mt);cursor:pointer;transition:.2s}
.chip:hover{border-color:var(--cy);color:var(--cy)}
</style></head><body>
<h1>&#9201; SMARTWATCH</h1>
<div class="sub">ESP32-C3 · SH1106 OLED</div>
<div class="pill"><div class="dot" id="dot"></div><span id="st">Connecting…</span></div>

<div class="card">
  <div class="lbl">&#128172; Send Message</div>
  <input id="msg" placeholder="Up to 20 characters…" maxlength="20">
  <button class="btn cy" onclick="sendMsg()">&#9654; Show on Watch</button>
  <button class="btn rd" onclick="c('CMD:CLEAR')">&#10005; Clear Screen</button>
</div>

<div class="card">
  <div class="lbl">&#128336; Clock Format</div>
  <div class="row">
    <button class="btn gh" onclick="c('CMD:12H')">12 H  AM/PM</button>
    <button class="btn gh" onclick="c('CMD:24H')">24 H</button>
  </div>
</div>

<div class="card">
  <div class="lbl">&#9201; Stopwatch</div>
  <div class="row">
    <button class="btn gr" onclick="c('CMD:SW_TOGGLE')">&#9654; / &#9646;&#9646; Toggle</button>
    <button class="btn gh" onclick="c('CMD:SW_RESET')">&#8635; Reset</button>
  </div>
</div>

<div class="card">
  <div class="lbl">&#9203; Countdown Timer</div>
  <input id="tsec" type="number" placeholder="Duration in seconds  (e.g. 300)" min="1" max="86400">
  <button class="btn cy"  onclick="setTimer()">&#9654; Set &amp; Start</button>
  <button class="btn gh" onclick="c('CMD:TIMER_STOP')">&#9632; Stop Timer</button>
</div>

<div class="card">
  <div class="lbl">&#128260; Switch Face</div>
  <div class="chips">
    <div class="chip" onclick="c('CMD:FACE_CLOCK')">&#128336; Clock</div>
    <div class="chip" onclick="c('CMD:FACE_STOPWATCH')">&#9201; Stopwatch</div>
    <div class="chip" onclick="c('CMD:FACE_TIMER')">&#9203; Timer</div>
    <div class="chip" onclick="c('CMD:FACE_MESSAGE')">&#128172; Message</div>
  </div>
</div>

<script>
var ws,retryT;
function connect(){
  clearTimeout(retryT);
  ws=new WebSocket('ws://'+location.hostname+':81/');
  ws.onopen=function(){document.getElementById('dot').className='dot on';document.getElementById('st').textContent='Connected to Watch';};
  ws.onclose=function(){document.getElementById('dot').className='dot';document.getElementById('st').textContent='Disconnected — retrying…';retryT=setTimeout(connect,3000);};
  ws.onerror=function(){ws.close();};
}
connect();
function c(cmd){if(ws&&ws.readyState===1)ws.send(cmd);}
function sendMsg(){var m=document.getElementById('msg').value.trim();if(m){c(m);document.getElementById('msg').value='';}}
function setTimer(){var s=parseInt(document.getElementById('tsec').value)||0;if(s>0)c('CMD:TIMER_SET:'+s);}
</script>
</body></html>
)HTMLPAGE";

// ================================================================
//  9. HELPERS
// ================================================================
void wakeScreen() { lastActivity = millis(); }

bool isTouched() {
  // Reads the digital HIGH/LOW from the TTP223 sensor
  return digitalRead(TOUCH_PIN) == HIGH;
}

String fmtDuration(unsigned long ms) {
  unsigned long s  = ms / 1000;
  unsigned int  hh = s / 3600;
  unsigned int  mm = (s % 3600) / 60;
  unsigned int  ss = s % 60;
  char buf[10];
  if (hh > 0) sprintf(buf, "%02u:%02u:%02u", hh, mm, ss);
  else        sprintf(buf, "%02u:%02u",       mm, ss);
  return String(buf);
}

// ================================================================
//  10. WEBSOCKET EVENTS
// ================================================================
void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t len) {
  if (type != WStype_TEXT) return;
  String cmd = String((char*)payload);
  wakeScreen();

  if      (cmd == "CMD:CLEAR")          { customMsg = ""; currentFace = FACE_CLOCK; }
  else if (cmd == "CMD:12H")            { use12h = true; }
  else if (cmd == "CMD:24H")            { use12h = false; }
  else if (cmd == "CMD:SW_TOGGLE") {
    currentFace = FACE_STOPWATCH;
    if (swRunning) { swSavedMs += millis() - swStartMs; swRunning = false; }
    else           { swStartMs  = millis(); swRunning = true; }
  }
  else if (cmd == "CMD:SW_RESET") {
    swRunning = false; swSavedMs = 0;
    currentFace = FACE_STOPWATCH;
  }
  else if (cmd.startsWith("CMD:TIMER_SET:")) {
    unsigned long secs = cmd.substring(14).toInt();
    if (secs > 0) {
      cdDuration  = secs * 1000UL;
      cdStartMs   = millis();
      cdRunning   = true;
      cdDone      = false;
      currentFace = FACE_TIMER;
    }
  }
  else if (cmd == "CMD:TIMER_STOP") {
    cdRunning = false; cdDone = false;
    currentFace = FACE_TIMER;
  }
  else if (cmd == "CMD:FACE_CLOCK")     { currentFace = FACE_CLOCK; }
  else if (cmd == "CMD:FACE_STOPWATCH") { currentFace = FACE_STOPWATCH; }
  else if (cmd == "CMD:FACE_TIMER")     { currentFace = FACE_TIMER; }
  else if (cmd == "CMD:FACE_MESSAGE")   { currentFace = FACE_MESSAGE; }
  else {
    customMsg   = cmd;
    currentFace = FACE_MESSAGE;
  }
}

// ================================================================
//  11. DRAW HELPERS
// ================================================================
void drawHeader(const char* t) {
  u8g2.setFont(u8g2_font_profont11_tr);
  int w = u8g2.getStrWidth(t);
  u8g2.drawStr((128 - w) / 2, 11, t);
  u8g2.drawHLine(0, 14, 128);
}

// ================================================================
//  12. DRAW FACES
// ================================================================
void drawClock() {
  struct tm t;
  if (!getLocalTime(&t)) {
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(14, 35, "Syncing NTP...");
    return;
  }
  char timeBuf[12], dateBuf[14], dayBuf[12];
  strftime(timeBuf, sizeof(timeBuf), use12h ? "%I:%M %p" : "%H:%M:%S", &t);
  strftime(dateBuf, sizeof(dateBuf), "%d %b %Y", &t);
  strftime(dayBuf,  sizeof(dayBuf),  "%A",        &t);

  u8g2.setFont(u8g2_font_logisoso24_tr);
  u8g2.drawStr(2, 40, timeBuf);
  u8g2.drawHLine(0, 43, 128);
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(2, 55, dayBuf);
  int dw = u8g2.getStrWidth(dateBuf);
  u8g2.drawStr(126 - dw, 55, dateBuf);
}

void drawStopwatch() {
  drawHeader("STOPWATCH");
  unsigned long elapsed = swSavedMs + (swRunning ? millis() - swStartMs : 0);
  String ts = fmtDuration(elapsed);
  u8g2.setFont(u8g2_font_logisoso24_tr);
  int tw = u8g2.getStrWidth(ts.c_str());
  u8g2.drawStr((128 - tw) / 2, 48, ts.c_str());
  u8g2.setFont(u8g2_font_profont11_tr);
  const char* s = swRunning ? ">  RUNNING" : "||  PAUSED";
  int sw2 = u8g2.getStrWidth(s);
  u8g2.drawStr((128 - sw2) / 2, 62, s);
}

void drawTimer() {
  drawHeader("TIMER");
  if (cdDone) {
    u8g2.setFont(u8g2_font_logisoso24_tr);
    u8g2.drawStr(26, 48, "DONE!");
    return;
  }
  if (!cdRunning) {
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(10, 36, "Set seconds in web,");
    u8g2.drawStr(10, 50, "then press Start.");
    return;
  }
  unsigned long elapsed = millis() - cdStartMs;
  if (elapsed >= cdDuration) { cdRunning = false; cdDone = true; return; }
  unsigned long remain = cdDuration - elapsed;
  String ts = fmtDuration(remain);
  u8g2.setFont(u8g2_font_logisoso24_tr);
  int tw = u8g2.getStrWidth(ts.c_str());
  u8g2.drawStr((128 - tw) / 2, 46, ts.c_str());
  // progress bar
  int barW = (int)(120.0f * (float)elapsed / (float)cdDuration);
  u8g2.drawFrame(4, 52, 120, 9);
  if (barW > 0) u8g2.drawBox(4, 52, barW, 9);
}

void drawMessage() {
  drawHeader("MESSAGE");
  if (customMsg.length() == 0) {
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(8, 38, "No message yet.");
    u8g2.drawStr(8, 52, "Send one via web.");
    return;
  }
  if (customMsg.length() <= 9) {
    u8g2.setFont(u8g2_font_ncenB14_tr);
    int tw = u8g2.getStrWidth(customMsg.c_str());
    u8g2.drawStr((128 - tw) / 2, 46, customMsg.c_str());
  } else {
    u8g2.setFont(u8g2_font_ncenB10_tr);
    String l1 = customMsg.substring(0, 13);
    String l2 = customMsg.substring(13);
    u8g2.drawStr(2, 34, l1.c_str());
    u8g2.drawStr(2, 52, l2.c_str());
  }
}

void drawBanner() {
  if (!showBanner) return;
  if (millis() - bannerStart > BANNER_MS) { showBanner = false; return; }
  u8g2.setDrawColor(1);
  u8g2.drawRBox(10, 50, 108, 13, 4);
  u8g2.setDrawColor(0);
  u8g2.setFont(u8g2_font_profont11_tr);
  const char* lbl = "< Back to Clock";
  int w = u8g2.getStrWidth(lbl);
  u8g2.drawStr((128 - w) / 2, 61, lbl);
  u8g2.setDrawColor(1);
}

// ================================================================
//  13. SETUP
// ================================================================
void setup() {
  Serial.begin(115200);
  
  // Tell the ESP32 to treat the touch pin as a digital input
  pinMode(TOUCH_PIN, INPUT);
  
  delay(300);
  Serial.println("\n=== SMARTWATCH v3.2 ===");

  u8g2.begin();

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(22, 32, "Booting...");
  u8g2.sendBuffer();

  // WiFi
  Serial.print("Connecting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(500); Serial.print("."); tries++;
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(10, 22, "Connecting WiFi");
    char progress[14] = "";
    for (int i = 0; i < min(tries, 12); i++) strcat(progress, ".");
    u8g2.drawStr(10, 38, progress);
    u8g2.sendBuffer();
  }

  if (WiFi.status() == WL_CONNECTED) {
    configTime(gmtOffset_sec, dstOffset_sec, ntpServer);
    String ip = WiFi.localIP().toString();
    Serial.println("\nIP: " + ip);
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(5, 14, "WiFi OK!");
    u8g2.drawStr(5, 28, "Open on phone:");
    u8g2.drawStr(5, 42, ip.c_str());
    u8g2.drawStr(5, 56, "DBL-TAP = Clock");
    u8g2.sendBuffer();
    delay(6000);
  } else {
    Serial.println("\nWiFi failed — offline");
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(10, 30, "WiFi failed.");
    u8g2.drawStr(10, 46, "Offline mode.");
    u8g2.sendBuffer();
    delay(2000);
  }

  httpServer.on("/", HTTP_GET, []() {
    httpServer.send_P(200, "text/html", PAGE);
  });
  httpServer.begin();

  wsServer.begin();
  wsServer.onEvent(onWsEvent);

  lastActivity = millis();
  Serial.println("Ready!");
}

// ================================================================
//  14. LOOP
// ================================================================
void loop() {
  wsServer.loop();
  httpServer.handleClient();

  unsigned long now = millis();

  // ---- TOUCH -------------------------------------------------------
  bool nowTouched = isTouched();

  if (nowTouched && !touchWasOn && (now - touchRisingTime > DEBOUNCE_MS)) {
    touchRisingTime = now;
    wakeScreen();

    if (pendingSingle && (now - lastTapTime <= DBL_WIN_MS)) {
      // DOUBLE TAP
      pendingSingle = false;
      if (currentFace != FACE_CLOCK) {
        currentFace  = FACE_CLOCK;
        showBanner   = true;
        bannerStart  = now;
      }
    } else {
      // Start waiting for possible second tap
      pendingSingle = true;
      lastTapTime   = now;
    }
    touchWasOn = true;
  }

  if (!nowTouched && touchWasOn) {
    touchWasOn = false;
  }

  // Single-tap fires after double-tap window expires
  if (pendingSingle && (now - lastTapTime > DBL_WIN_MS)) {
    pendingSingle = false;
    currentFace = (WatchFace)((currentFace + 1) % FACE_COUNT);
  }
  // ---- END TOUCH ---------------------------------------------------

  // ---- SCREEN ------------------------------------------------------
  bool on = (now - lastActivity < SLEEP_MS);
  u8g2.setPowerSave(!on);

  if (on) {
    u8g2.clearBuffer();
    switch (currentFace) {
      case FACE_CLOCK:      drawClock();      break;
      case FACE_STOPWATCH:  drawStopwatch();  break;
      case FACE_TIMER:      drawTimer();      break;
      case FACE_MESSAGE:    drawMessage();    break;
      default:              drawClock();      break;
    }
    drawBanner();
    u8g2.sendBuffer();
  }
  // ---- END SCREEN --------------------------------------------------

  delay(40);
}
