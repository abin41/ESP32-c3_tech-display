/*
 * ================================================================
 * SMARTWATCH FIRMWARE v4.5  —  TRUE SETUP EDITION
 * Hardware : ESP32-C3 Super Mini
 * SH1106 1.3" OLED  (I2C: SDA=GPIO6  SCL=GPIO7)
 * TTP223 Touch Sensor on GPIO3
 *
 * FIXED IN v4.5:
 * - Removed hardcoded "afeez" defaults.
 * - Out-of-the-box, the watch now INSTANTLY goes to AP Mode 
 * ("SmartWatch-v4") so you can configure it immediately.
 * ================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <time.h>
#include <sys/time.h>    
#include <Preferences.h> 

// ================================================================
//  1.  MEMORY & WIFI STATE
// ================================================================
Preferences preferences;
String current_ssid = "";
String current_pass = "";
bool isAPMode = false;

// ================================================================
//  2.  TIME  (India Standard Time = UTC +5:30)
// ================================================================
const char* ntpServer     = "pool.ntp.org";
const long  gmtOffset_sec = 19800;
const int   dstOffset_sec = 0;

// ================================================================
//  3.  PINS
// ================================================================
#define TOUCH_PIN  3
#define SDA_PIN    6
#define SCL_PIN    7

// ================================================================
//  4.  DISPLAY (HW_I2C)
// ================================================================
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// ================================================================
//  5.  SERVERS
// ================================================================
WebServer        httpServer(80);
WebSocketsServer wsServer(81);

// ================================================================
//  6.  WATCH FACES
// ================================================================
enum WatchFace { FACE_CLOCK=0, FACE_STOPWATCH, FACE_TIMER,
                 FACE_MESSAGE, FACE_SETTINGS, FACE_COUNT };
WatchFace currentFace = FACE_CLOCK;

// ================================================================
//  7.  STATE VARIABLES
// ================================================================
unsigned long lastActivity   = 0;
unsigned long SLEEP_MS       = 20000;

bool   use12h    = false;
String customMsg = "";

// Stopwatch
bool          swRunning = false;
unsigned long swStartMs = 0;
unsigned long swSavedMs = 0;
String        swLaps[20];
int           swLapCount = 0;

// Countdown timer
unsigned long cdDuration = 60000;
unsigned long cdStartMs  = 0;
bool          cdRunning  = false;
bool          cdDone     = false;

// Touch
bool          touchWasOn      = false;
unsigned long touchRisingTime = 0;
unsigned long lastTapTime     = 0;
bool          pendingSingle   = false;
bool          longPressHandled= false; 
const unsigned long DEBOUNCE_MS   = 50;
const unsigned long DBL_WIN_MS    = 350;
const unsigned long LONG_PRESS_MS = 600; 

// Home banner
bool          showBanner  = false;
unsigned long bannerStart = 0;
const unsigned long BANNER_MS = 800;

// Brightness & Power
uint8_t brightness = 8;
bool    screenWasOn = true;

// ================================================================
//  8.  WEB PAGE
// ================================================================
const char PAGE[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SmartWatch v4.5</title>
<style>
:root{--bg:#0d0d14;--card:#13131f;--bd:#252535;--cy:#00e5ff;--rd:#ff4060;--gr:#39ff7a;--am:#ffb800;--tx:#e0e0f0;--mt:#44445a;--dim:#1a1a2a}
*{box-sizing:border-box;margin:0;padding:0}html{font-size:15px}body{background:var(--bg);color:var(--tx);font-family:system-ui,sans-serif;padding:12px 12px 60px;max-width:480px;margin:0 auto}h1{text-align:center;font-family:monospace;letter-spacing:3px;color:var(--cy);margin:14px 0 2px;font-size:18px}.sub{text-align:center;color:var(--mt);font-size:11px;margin-bottom:14px}
.watch-wrap{background:#000;border-radius:20px;padding:18px;margin-bottom:14px;border:2px solid #2a2a2a}.oled{background:#000;border-radius:8px;padding:14px;min-height:110px;font-family:monospace;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:5px;border:1px solid #111;transition:opacity .3s}.oled-time{font-size:34px;font-weight:bold;color:#fff;letter-spacing:2px;line-height:1}.oled-ampm{font-size:13px;color:#888;margin-left:4px}.oled-sep{width:90%;height:1px;background:#222;margin:3px 0}.oled-row{display:flex;justify-content:space-between;width:90%;font-size:10px}.oled-day{color:#777}.oled-date{color:#aaa}.oled-lbl{font-size:9px;color:#555;letter-spacing:2px;border-bottom:1px solid #1a1a1a;width:100%;text-align:center;padding-bottom:5px}.oled-big{font-size:28px;color:#fff;font-weight:bold;letter-spacing:2px}.oled-sub{font-size:9px;color:#555;letter-spacing:1px}.oled-msg{font-size:12px;color:#fff;text-align:center;line-height:1.4;max-width:180px}.oled-bar-bg{width:90%;height:6px;border:1px solid #333;border-radius:1px;margin-top:4px}.oled-bar-fill{height:6px;background:#fff;border-radius:1px;transition:width .8s linear}.oled-done{font-size:26px;color:#fff;letter-spacing:3px}.face-dots{display:flex;justify-content:center;gap:7px;margin-top:10px}.fdot{width:5px;height:5px;border-radius:50%;background:#2a2a2a;transition:.2s}.fdot.on{background:#fff}.hw-btns{display:flex;justify-content:space-between;padding:10px 10px 0}.hw-btn{padding:5px 14px;font-size:10px;font-family:monospace;background:transparent;border:1px solid #333;border-radius:20px;color:#666;cursor:pointer;letter-spacing:1px;transition:.15s}.hw-btn:hover{border-color:#777;color:#bbb}.hw-btn:active{transform:scale(.95)}
.pill{display:flex;align-items:center;gap:8px;background:var(--card);border:1px solid var(--bd);border-radius:999px;padding:6px 16px;font-size:12px;font-family:monospace;margin-bottom:14px;justify-content:center}.dot{width:8px;height:8px;border-radius:50%;background:var(--mt);transition:.3s}.dot.on{background:var(--gr);box-shadow:0 0 6px var(--gr)}
.card{background:var(--card);border:1px solid var(--bd);border-radius:14px;padding:14px;margin-bottom:10px}.lbl{font-size:10px;font-family:monospace;color:var(--mt);letter-spacing:2px;text-transform:uppercase;margin-bottom:10px}input[type=text],input[type=number],input[type=password]{width:100%;padding:10px 11px;font-size:14px;background:#0a0a12;border:1px solid var(--bd);border-radius:8px;color:var(--tx);outline:none;margin-bottom:8px;font-family:monospace}input[type=text]:focus,input[type=number]:focus,input[type=password]:focus{border-color:var(--cy)}.btn{display:block;width:100%;padding:10px;font-size:13px;font-weight:700;border:none;border-radius:8px;cursor:pointer;margin-bottom:8px;transition:opacity .15s,transform .1s;font-family:system-ui,sans-serif}.btn:active{opacity:.75;transform:scale(.98)}.btn:last-child{margin-bottom:0}.cy{background:var(--cy);color:#000}.rd{background:var(--rd);color:#fff}.gr{background:var(--gr);color:#000}.am{background:var(--am);color:#000}.gh{background:transparent;color:var(--tx);border:1px solid var(--bd)}.gh:hover{border-color:#555}.row{display:grid;grid-template-columns:1fr 1fr;gap:8px}.chips{display:flex;flex-wrap:wrap;gap:6px;margin-top:2px}.chip{padding:5px 13px;border-radius:999px;font-size:11px;font-family:monospace;background:var(--dim);border:1px solid var(--bd);color:var(--mt);cursor:pointer;transition:.2s}.chip:hover,.chip.active{border-color:var(--cy);color:var(--cy);background:#0a1820}.toggles{display:flex;gap:6px}.tgl{padding:7px 16px;border-radius:999px;font-size:12px;font-family:monospace;background:var(--dim);border:1px solid var(--bd);color:var(--mt);cursor:pointer;transition:.2s}.tgl.active{background:var(--cy);color:#000;border-color:var(--cy)}
.sw-disp{font-family:monospace;font-size:30px;font-weight:bold;text-align:center;padding:10px 0;letter-spacing:2px;color:var(--cy)}.sw-status{text-align:center;font-size:10px;font-family:monospace;color:var(--mt);margin-bottom:8px}.lap-list{max-height:70px;overflow-y:auto;margin-top:6px;font-size:11px;font-family:monospace;color:var(--mt);line-height:1.8}
.timer-inputs{display:grid;grid-template-columns:1fr 1fr 1fr auto;gap:6px;margin-bottom:8px}.timer-inputs input{margin:0}.presets{display:flex;flex-wrap:wrap;gap:5px;margin-bottom:10px}.preset{padding:4px 11px;border-radius:999px;font-size:11px;font-family:monospace;background:var(--dim);border:1px solid var(--bd);color:var(--mt);cursor:pointer;transition:.2s}.preset:hover{border-color:var(--am);color:var(--am)}.tbar{width:100%;height:5px;background:#1a1a2a;border-radius:3px;margin:6px 0;overflow:hidden}.tbar-fill{height:5px;background:var(--cy);border-radius:3px;transition:width .8s linear}.tbar-info{display:flex;justify-content:space-between;font-size:10px;font-family:monospace;color:var(--mt)}
.char-count{font-size:10px;font-family:monospace;color:var(--mt);text-align:right;margin-top:-6px;margin-bottom:8px}.msg-presets{display:flex;flex-wrap:wrap;gap:5px;margin-bottom:10px}
.slider-row{display:flex;align-items:center;gap:10px;margin-bottom:10px}.slider-row label{font-size:12px;font-family:monospace;color:var(--mt);min-width:80px}input[type=range]{flex:1;accent-color:var(--cy);height:4px;cursor:pointer}.slider-val{font-size:12px;font-family:monospace;color:var(--cy);min-width:32px;text-align:right}
.log{background:#080810;border-radius:8px;padding:8px 10px;font-size:10px;font-family:monospace;color:#333;max-height:90px;overflow-y:auto;line-height:1.7;border:1px solid #1a1a2a}.log-line{color:#555}.log-line b{color:#888}
</style></head><body>

<h1>&#9201; SMARTWATCH v4.5</h1>
<div class="sub">TRUE SETUP EDITION</div>

<div class="watch-wrap">
  <div class="oled" id="oled">
    <div class="oled-time" id="ot">--:--:--</div>
    <div class="oled-sep"></div>
    <div class="oled-row"><div class="oled-day" id="od">---</div><div class="oled-date" id="odate">-- --- ----</div></div>
  </div>
  <div class="face-dots">
    <div class="fdot on" id="fd0"></div><div class="fdot" id="fd1"></div><div class="fdot" id="fd2"></div><div class="fdot" id="fd3"></div><div class="fdot" id="fd4"></div>
  </div>
  <div class="hw-btns">
    <button class="hw-btn" onclick="hwTap()">TAP</button>
    <button class="hw-btn" onclick="hwDbl()">DBL TAP</button>
  </div>
</div>

<div class="pill"><div class="dot" id="dot"></div><span id="st">Connecting…</span></div>

<div class="card">
  <div class="lbl">&#128260; Watch Face</div>
  <div class="chips">
    <div class="chip active" id="fc0" onclick="gotoFace('CLOCK',0)">&#128336; Clock</div>
    <div class="chip" id="fc1" onclick="gotoFace('STOPWATCH',1)">&#9201; Stopwatch</div>
    <div class="chip" id="fc2" onclick="gotoFace('TIMER',2)">&#9203; Timer</div>
    <div class="chip" id="fc3" onclick="gotoFace('MESSAGE',3)">&#128172; Message</div>
    <div class="chip" id="fc4" onclick="gotoFace('SETTINGS',4)">&#9881; Settings</div>
  </div>
</div>

<div class="card">
  <div class="lbl">&#128246; Watch Wi-Fi Setup</div>
  <input type="text" id="w-ssid" placeholder="Network Name (SSID)">
  <input type="password" id="w-pass" placeholder="Wi-Fi Password">
  <button class="btn am" onclick="saveWifi()">Save & Reboot Watch</button>
  <p style="font-size:10px;color:var(--mt);text-align:center;margin-top:6px">Watch will restart to apply new Wi-Fi settings.</p>
</div>

<div class="card">
  <div class="lbl">&#9201; Stopwatch</div>
  <div class="sw-disp" id="sw-disp">00:00.00</div>
  <div class="sw-status" id="sw-st">PAUSED &nbsp;·&nbsp; <span id="sw-laps">0 laps</span></div>
  <div class="row">
    <button class="btn gr" id="sw-btn" onclick="swToggle()">&#9654; Start</button>
    <button class="btn gh" onclick="swLap()">&#9678; Lap</button>
  </div>
  <button class="btn rd" style="margin-top:8px" onclick="swReset()">&#8635; Reset</button>
  <div class="lap-list" id="lap-list"></div>
</div>

<div class="card">
  <div class="lbl">&#9203; Countdown Timer</div>
  <div class="presets" id="tpresets"></div>
  <div class="timer-inputs">
    <input type="number" id="cdh" placeholder="h" min="0" max="23">
    <input type="number" id="cdm" placeholder="m" min="0" max="59">
    <input type="number" id="cds" placeholder="s" min="0" max="59">
    <button class="btn cy" style="margin:0;padding:10px 0;font-size:12px" onclick="cdStart()">&#9654;</button>
  </div>
  <div class="tbar"><div class="tbar-fill" id="tbar-fill" style="width:0%"></div></div>
  <div class="tbar-info"><span id="cd-st">Not running</span><span id="cd-rem"></span></div>
  <button class="btn rd" style="margin-top:10px" onclick="cdStop()">&#9632; Stop Timer</button>
</div>

<div class="card">
  <div class="lbl">&#128172; Message</div>
  <input type="text" id="msg" placeholder="Up to 20 characters…" maxlength="20" oninput="msgInput()">
  <div class="char-count" id="cc">20 left</div>
  <button class="btn cy" onclick="sendMsg()">&#9654; Show on Watch</button>
  <button class="btn rd" onclick="clearMsg()">&#10005; Clear</button>
</div>

<div class="card">
  <div class="lbl">&#9881; Display Settings</div>
  <div class="slider-row">
    <label>Brightness</label>
    <input type="range" min="1" max="10" value="8" id="br-sl" oninput="setBright(this.value)">
    <span class="slider-val" id="br-val">8</span>
  </div>
  <div class="slider-row">
    <label>Sleep delay</label>
    <input type="range" min="5" max="60" value="20" step="5" id="sl-sl" oninput="setSleep(this.value)">
    <span class="slider-val" id="sl-val">20s</span>
  </div>
  <div class="toggles" style="margin-top:10px">
    <button class="tgl active" id="t24h" onclick="c('CMD:24H');setFmt(false)">24H Clock</button>
    <button class="tgl" id="t12h" onclick="c('CMD:12H');setFmt(true)">12H Clock</button>
  </div>
</div>

<script>
var ws,rtT;
var WATCH_IP = location.hostname || '192.168.4.1'; 

var state={face:'CLOCK',use12h:false,msg:'',swRun:false,swStart:0,swSaved:0,swLaps:[],cdDur:0,cdStart:0,cdRun:false,cdDone:false,bright:8};
var FACES=['CLOCK','STOPWATCH','TIMER','MESSAGE','SETTINGS'];
var DAYS=['Sunday','Monday','Tuesday','Wednesday','Thursday','Friday','Saturday'];
var MONTHS=['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec'];

function connect(){
  clearTimeout(rtT);
  if(ws) ws.close();
  ws=new WebSocket('ws://'+WATCH_IP+':81/');
  ws.onopen=function(){D('dot').className='dot on';D('st').textContent='Connected';};
  ws.onclose=function(){D('dot').className='dot';D('st').textContent='Disconnected...';rtT=setTimeout(connect,3000);};
  ws.onerror=function(){ws.close();};
}
window.onload=connect;

function saveWifi() {
  var s = D('w-ssid').value.trim();
  var p = D('w-pass').value.trim();
  if(!s) { alert("Please enter a Network Name (SSID)"); return; }
  c("CMD:WIFI:" + s + "\n" + p);
  alert("Credentials sent! Watch will reboot now. Reconnect to your home Wi-Fi.");
}

function D(id){return document.getElementById(id);}
function c(cmd){if(ws&&ws.readyState===1)ws.send(cmd);}

function fmtMs(ms){var s=Math.floor(ms/1000),hh=Math.floor(s/3600),mm=Math.floor((s%3600)/60),ss=s%60,cs=Math.floor((ms%1000)/10);if(hh>0)return pad(hh)+':'+pad(mm)+':'+pad(ss);return pad(mm)+':'+pad(ss)+'.'+pad(cs);}
function fmtMsShort(ms){var s=Math.floor(ms/1000),hh=Math.floor(s/3600),mm=Math.floor((s%3600)/60),ss=s%60;if(hh>0)return pad(hh)+':'+pad(mm)+':'+pad(ss);return pad(mm)+':'+pad(ss);}
function pad(n){return n.toString().padStart(2,'0');}

function setFmt(h12){state.use12h=h12;D('t12h').className='tgl'+(h12?' active':'');D('t24h').className='tgl'+(!h12?' active':'');}
function gotoFace(f,i){c('CMD:FACE_'+f);state.face=f;for(var j=0;j<5;j++){D('fd'+j).className='fdot'+(j===i?' on':'');D('fc'+j).className='chip'+(j===i?' active':'');}renderOled();}
function hwTap(){var i=FACES.indexOf(state.face);gotoFace(FACES[(i+1)%5],(i+1)%5);}
function hwDbl(){gotoFace('CLOCK',0);}

function swToggle(){if(state.swRun){state.swSaved+=Date.now()-state.swStart;state.swRun=false;c('CMD:SW_TOGGLE');D('sw-btn').innerHTML='&#9654; Start';D('sw-btn').className='btn gr';}else{state.swStart=Date.now();state.swRun=true;c('CMD:SW_TOGGLE');D('sw-btn').innerHTML='&#9646;&#9646; Pause';D('sw-btn').className='btn am';gotoFace('STOPWATCH',1);}}
function swLap(){var e=state.swSaved+(state.swRun?Date.now()-state.swStart:0);state.swLaps.push(e);var i=state.swLaps.length;D('lap-list').innerHTML='<div>Lap '+i+' — '+fmtMs(e)+'</div>'+D('lap-list').innerHTML;D('sw-laps').textContent=i+' lap'+(i===1?'':'s');c('CMD:SW_LAP');}
function swReset(){state.swRun=false;state.swSaved=0;state.swStart=0;state.swLaps=[];D('sw-btn').innerHTML='&#9654; Start';D('sw-btn').className='btn gr';D('sw-disp').textContent='00:00.00';D('lap-list').innerHTML='';D('sw-laps').textContent='0 laps';c('CMD:SW_RESET');}

var TPRESETS=[{l:'1 min',s:60},{l:'5 min',s:300},{l:'10 min',s:600},{l:'30 min',s:1800}];
(function(){var el=D('tpresets');TPRESETS.forEach(function(p){var d=document.createElement('div');d.className='preset';d.textContent=p.l;d.onclick=function(){var h=Math.floor(p.s/3600),m=Math.floor((p.s%3600)/60),s=p.s%60;D('cdh').value=h||'';D('cdm').value=m||'';D('cds').value=s||'';cdStart();};el.appendChild(d);});})();

function cdStart(){var h=parseInt(D('cdh').value)||0,m=parseInt(D('cdm').value)||0,s=parseInt(D('cds').value)||0,tot=(h*3600+m*60+s)*1000;if(tot<=0)return;state.cdDur=tot;state.cdStart=Date.now();state.cdRun=true;state.cdDone=false;c('CMD:TIMER_SET:'+(h*3600+m*60+s));gotoFace('TIMER',2);}
function cdStop(){state.cdRun=false;state.cdDone=false;D('tbar-fill').style.width='0%';D('cd-st').textContent='Stopped';D('cd-rem').textContent='';c('CMD:TIMER_STOP');}

function msgInput(){D('cc').textContent=(20-D('msg').value.length)+' left';}
function sendMsg(){var m=D('msg').value.trim();if(!m)return;state.msg=m;c(m);gotoFace('MESSAGE',3);D('msg').value='';D('cc').textContent='20 left';}
function clearMsg(){state.msg='';c('CMD:CLEAR');gotoFace('CLOCK',0);D('msg').value='';}

function setBright(v){state.bright=parseInt(v);D('br-val').textContent=v;D('oled').style.opacity=0.3+0.07*v;c('CMD:BRIGHTNESS:'+v);}
function setSleep(v){D('sl-val').textContent=v+'s';c('CMD:SLEEP:'+v);}

function renderOled(){
  var o=D('oled'),h='';
  if(state.face==='CLOCK'){
    var now=new Date(),hr=now.getHours(),mi=now.getMinutes(),sc=now.getSeconds(),ampm='';
    if(state.use12h){ampm='<span class="oled-ampm">'+(hr>=12?'PM':'AM')+'</span>';hr=hr%12||12;}
    var ds=now.getDate().toString().padStart(2,'0')+' '+MONTHS[now.getMonth()]+' '+now.getFullYear();
    h='<div class="oled-time">'+pad(hr)+':'+pad(mi)+':'+pad(sc)+ampm+'</div><div class="oled-sep"></div><div class="oled-row"><div class="oled-day">'+DAYS[now.getDay()]+'</div><div class="oled-date">'+ds+'</div></div>';
  }else if(state.face==='STOPWATCH'){
    var e=state.swSaved+(state.swRun?Date.now()-state.swStart:0);
    h='<div class="oled-lbl">STOPWATCH</div><div class="oled-big">'+fmtMs(e)+'</div><div class="oled-sub">'+(state.swRun?'RUNNING':'PAUSED')+'</div>';
  }else if(state.face==='TIMER'){
    if(state.cdDone) h='<div class="oled-lbl">TIMER</div><div class="oled-done">DONE!</div>';
    else if(!state.cdRun) h='<div class="oled-lbl">TIMER</div><div class="oled-msg" style="color:#555;font-size:11px">Set time above,<br>then press Start.</div>';
    else{var el=Date.now()-state.cdStart,rem=Math.max(0,state.cdDur-el),pct=Math.min(100,Math.round(100*el/state.cdDur));h='<div class="oled-lbl">TIMER</div><div class="oled-big">'+fmtMsShort(rem)+'</div><div class="oled-bar-bg"><div class="oled-bar-fill" style="width:'+pct+'%"></div></div>';}
  }else if(state.face==='MESSAGE'){
    h=state.msg?'<div class="oled-lbl">MESSAGE</div><div class="oled-msg">'+state.msg+'</div>':'<div class="oled-lbl">MESSAGE</div><div class="oled-msg" style="color:#555">No message yet.</div>';
  }else if(state.face==='SETTINGS'){
    h='<div class="oled-lbl">SETTINGS</div><div class="oled-msg" style="margin-top:5px;font-size:10px;">IP Address:</div><div class="oled-sub" style="color:#00e5ff;font-size:12px;margin:3px 0;">'+WATCH_IP+'</div><div class="oled-msg" style="color:#777;font-size:9px;margin-top:5px;">Hold touch to Reboot</div>';
  }
  o.innerHTML=h;
}

setInterval(function(){
  if(state.face==='CLOCK'||state.face==='STOPWATCH') renderOled();
  if(state.swRun) D('sw-disp').textContent=fmtMs(state.swSaved+(Date.now()-state.swStart));
  if(state.cdRun){
    var el=Date.now()-state.cdStart,rem=Math.max(0,state.cdDur-el),pct=Math.min(100,Math.round(100*el/state.cdDur));
    D('tbar-fill').style.width=pct+'%';D('cd-rem').textContent=fmtMsShort(rem)+' left';D('cd-st').textContent='Running';
    if(state.face==='TIMER')renderOled();
    if(rem<=0){state.cdRun=false;state.cdDone=true;D('cd-st').textContent='Done!';D('cd-rem').textContent='';}
  }
},100);
renderOled();
</script>
</body></html>
)HTMLPAGE";

// ================================================================
//  9. HELPERS
// ================================================================
void wakeScreen() { lastActivity = millis(); }

bool isTouched() { return digitalRead(TOUCH_PIN) == HIGH; }

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

void applyBrightness() {
  uint8_t contrast = (uint8_t)map((long)brightness, 1, 10, 10, 255);
  u8g2.setContrast(contrast);
}

// ================================================================
//  10. WEBSOCKET EVENTS
// ================================================================
void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t len) {
  if (type != WStype_TEXT) return;
  String cmd = String((char*)payload);
  wakeScreen();

  if (cmd.startsWith("CMD:WIFI:")) {
    String data = cmd.substring(9);
    int split = data.indexOf('\n'); 
    if (split != -1) {
      String newSsid = data.substring(0, split);
      String newPass = data.substring(split + 1);
      
      preferences.putString("ssid", newSsid);
      preferences.putString("pass", newPass);
      
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_ncenB08_tr);
      u8g2.drawStr(10, 30, "Wi-Fi Saved!");
      u8g2.drawStr(10, 46, "Rebooting...");
      u8g2.sendBuffer();
      delay(1000);
      ESP.restart(); 
    }
  }
  else if (cmd == "CMD:CLEAR")          { customMsg = ""; currentFace = FACE_CLOCK; }
  else if (cmd == "CMD:12H")            { use12h = true; }
  else if (cmd == "CMD:24H")            { use12h = false; }
  else if (cmd == "CMD:REBOOT")         { ESP.restart(); } 
  else if (cmd == "CMD:SW_TOGGLE") {
    currentFace = FACE_STOPWATCH;
    if (swRunning) { swSavedMs += millis() - swStartMs; swRunning = false; }
    else           { swStartMs  = millis(); swRunning = true; }
  }
  else if (cmd == "CMD:SW_LAP") { }
  else if (cmd == "CMD:SW_RESET") {
    swRunning = false; swSavedMs = 0; swLapCount = 0;
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
  else if (cmd.startsWith("CMD:BRIGHTNESS:")) {
    int b = cmd.substring(15).toInt();
    if (b >= 1 && b <= 10) { brightness = (uint8_t)b; applyBrightness(); }
  }
  else if (cmd.startsWith("CMD:SLEEP:")) {
    unsigned long s = cmd.substring(10).toInt();
    if (s >= 5 && s <= 3600) SLEEP_MS = s * 1000UL;
  }
  else if (cmd == "CMD:FACE_CLOCK")     { currentFace = FACE_CLOCK; }
  else if (cmd == "CMD:FACE_STOPWATCH") { currentFace = FACE_STOPWATCH; }
  else if (cmd == "CMD:FACE_TIMER")     { currentFace = FACE_TIMER; }
  else if (cmd == "CMD:FACE_MESSAGE")   { currentFace = FACE_MESSAGE; }
  else if (cmd == "CMD:FACE_SETTINGS")  { currentFace = FACE_SETTINGS; }
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
  
  if (!getLocalTime(&t, 10)) {
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

  if (elapsed < 3600000UL) {
    char buf[12];
    unsigned int cs = (elapsed % 1000) / 10;
    sprintf(buf, "%s.%02u", ts.c_str(), cs);
    ts = String(buf);
  }

  u8g2.setFont(u8g2_font_logisoso20_tr);
  int tw = u8g2.getStrWidth(ts.c_str());
  u8g2.drawStr((128 - tw) / 2, 46, ts.c_str());
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
    u8g2.drawStr(10, 36, "Set time in web,");
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

void drawSettings() {
  drawHeader("SETTINGS");
  u8g2.setFont(u8g2_font_profont11_tr);
  
  if (isAPMode) {
    u8g2.drawStr(5, 28, "Mode: Setup (AP)");
    u8g2.drawStr(5, 42, "IP: 192.168.4.1");
  } else {
    u8g2.drawStr(5, 28, "Mode: Connected");
    String ip = WiFi.localIP().toString();
    u8g2.drawStr(5, 42, ip.c_str());
  }
  
  u8g2.drawStr(5, 60, "Hold touch to Reboot");
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
  
  pinMode(TOUCH_PIN, INPUT_PULLDOWN); 
  delay(300);
  
  Wire.begin(SDA_PIN, SCL_PIN);
  u8g2.begin();
  applyBrightness();

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(22, 32, "Booting v4.5...");
  u8g2.sendBuffer();

  preferences.begin("watch_cfg", false);
  // Default to empty strings so it forces AP mode if not configured!
  current_ssid = preferences.getString("ssid", "");
  current_pass = preferences.getString("pass", "");

  WiFi.mode(WIFI_STA);
  
  if (current_ssid != "") {
    u8g2.clearBuffer();
    u8g2.drawStr(10, 22, "Connecting to:");
    u8g2.drawStr(10, 38, current_ssid.c_str());
    u8g2.sendBuffer();
    
    WiFi.begin(current_ssid.c_str(), current_pass.c_str());
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 30) {
      delay(500); tries++;
    }
  }

  // ---- AP FALLBACK & WEBSOCKET FIX ----
  if (WiFi.status() != WL_CONNECTED) {
    isAPMode = true;
    
    WiFi.disconnect(true); 
    delay(100); 

    WiFi.mode(WIFI_AP);
    WiFi.softAP("SmartWatch-v4", ""); 

    struct timeval tv;
    tv.tv_sec = 1704067200; // Jan 1 2024 00:00:00
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(5, 15, "No Wi-Fi Found.");
    u8g2.drawStr(5, 30, "Connect phone to:");
    u8g2.drawStr(5, 45, "SmartWatch-v4");
    u8g2.drawStr(5, 60, "Open: 192.168.4.1");
    u8g2.sendBuffer();
    delay(5000);
  } else {
    configTime(gmtOffset_sec, dstOffset_sec, ntpServer);
    String ip = WiFi.localIP().toString();
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(5, 14, "Wi-Fi Connected!");
    u8g2.drawStr(5, 28, "Open on phone:");
    u8g2.drawStr(5, 42, ip.c_str());
    u8g2.drawStr(5, 56, "DBL-TAP = Clock");
    u8g2.sendBuffer();
    delay(4000);
  }

  httpServer.on("/", HTTP_GET, []() {
    httpServer.send_P(200, "text/html", PAGE);
  });
  httpServer.begin();
  wsServer.begin();
  wsServer.onEvent(onWsEvent);

  lastActivity = millis();
}

// ================================================================
//  14. LOOP
// ================================================================
void loop() {
  wsServer.loop();
  httpServer.handleClient();

  unsigned long now = millis();

  // ---- TOUCH LOGIC ------------------------
  bool nowTouched = isTouched();

  if (nowTouched && !touchWasOn && (now - touchRisingTime > DEBOUNCE_MS)) {
    touchRisingTime = now;
    longPressHandled = false;
    wakeScreen();

    if (pendingSingle && (now - lastTapTime <= DBL_WIN_MS)) {
      pendingSingle = false;
      if (currentFace != FACE_CLOCK) {
        currentFace  = FACE_CLOCK;
        showBanner   = true;
        bannerStart  = now;
      }
    } else {
      pendingSingle = true;
      lastTapTime   = now;
    }
    touchWasOn = true;
  }

  if (nowTouched && touchWasOn && !longPressHandled && (now - touchRisingTime > LONG_PRESS_MS)) {
    longPressHandled = true;
    pendingSingle = false; 
    wakeScreen();

    if (currentFace == FACE_STOPWATCH) {
      if (swRunning) { swSavedMs += millis() - swStartMs; swRunning = false; }
      else           { swStartMs = millis(); swRunning = true; }
    } 
    else if (currentFace == FACE_TIMER) {
      if (cdRunning) { cdRunning = false; }
      else if (cdDuration > 0 && !cdDone) { cdStartMs = millis(); cdRunning = true; }
    }
    else if (currentFace == FACE_SETTINGS) {
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_ncenB08_tr);
      u8g2.drawStr(25, 38, "REBOOTING...");
      u8g2.sendBuffer();
      delay(500);
      ESP.restart(); 
    }
    
    showBanner = true;
    bannerStart = now;
  }

  if (!nowTouched && touchWasOn) {
    touchWasOn = false;
  }

  if (pendingSingle && !longPressHandled && (now - lastTapTime > DBL_WIN_MS)) {
    pendingSingle = false;
    currentFace = (WatchFace)((currentFace + 1) % FACE_COUNT);
  }

  // ---- SMART SCREEN RENDERER ---------------------------------------
  bool screenIsOn = (now - lastActivity < SLEEP_MS);
  
  if (screenIsOn != screenWasOn) {
    u8g2.setPowerSave(!screenIsOn);
    screenWasOn = screenIsOn;
  }

  if (screenIsOn) {
    static unsigned long lastDraw = 0;
    
    unsigned long drawInterval = 1000; 
    if (showBanner) drawInterval = 50; 
    else if (currentFace == FACE_STOPWATCH && swRunning) drawInterval = 50; 
    else if (currentFace == FACE_TIMER && cdRunning) drawInterval = 200; 

    if (now - lastDraw >= drawInterval) {
      lastDraw = now;
      u8g2.clearBuffer();
      switch (currentFace) {
        case FACE_CLOCK:      drawClock();      break;
        case FACE_STOPWATCH:  drawStopwatch();  break;
        case FACE_TIMER:      drawTimer();      break;
        case FACE_MESSAGE:    drawMessage();    break;
        case FACE_SETTINGS:   drawSettings();   break;
        default:              drawClock();      break;
      }
      drawBanner();
      u8g2.sendBuffer();
    }
  }

  yield(); 
}
