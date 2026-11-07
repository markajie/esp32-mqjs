/*
 * main/js/main.js - factory recovery script.
 *
 * The engine runs /spiffs/user.js first.  If that file is missing or throws,
 * it runs this embedded script instead, so this script must not depend on the
 * network, on credentials, or on any state a previous script left behind.
 *
 * It opens a WPA2 access point and the script uploader, then shows both on
 * the panel.  Join the AP from a phone, open the address, and replace the
 * broken /spiffs/user.js.  Connecting to an existing network is a user.js
 * concern (wifi.connect); recovery always stays AP-only so it works offline.
 * Every hardware call is guarded and a watchdog retries the access point and
 * the web server, so one failed subsystem can never hide the repair page.
 */
var AP_SSID = "Moji2-C5";
var AP_PASS = "12345678";
var AP_URL = "http://192.168.4.1";
var WEB_PORT = 80;
var RETRY_MS = 5000;

/* Timer slots in components/mquickjs/mqjs_engine/mjs_engine.c (MJS_MAX_TIMERS). */
var TIMER_SLOTS = 32;

var displayOK = false;
var apOK = false;
var apFailed = false;
var webOK = false;
var webFailed = false;

function apText() {
  if (apOK) return "WiFi 热点: 已开启";
  if (apFailed) return "WiFi 热点: 启动失败";
  return "WiFi 热点: 启动中";
}

function webText() {
  if (webOK) return "网页: 已开启";
  if (webFailed) return "网页: 启动失败";
  return "网页: 启动中";
}

function freeKB() {
  var kb = 0;
  try {
    kb = (system.freeHeap() / 1024) | 0;
  } catch (heapError) {
    kb = 0;
  }
  return kb;
}

/* One pass over the framebuffer, then a single flush.  fillRect() (not
 * fill()) is used for the background so the panel is not presented twice. */
function draw() {
  if (!displayOK) return;
  try {
    var w = display.width();
    var h = display.height();
    var bg = display.color(8, 18, 32);
    var bar = display.color(0, 88, 150);
    var text = display.color(255, 255, 255);
    var dim = display.color(150, 176, 204);
    var ok = display.color(80, 230, 140);
    var wait = display.color(255, 200, 60);
    var err = display.color(255, 96, 96);
    display.fillRect(0, 0, w, h, bg);
    display.fillRect(0, 0, w, 46, bar);
    display.fillRect(0, 46, w, 2, dim);
    display.text(12, 8, "MQuickJS 恢复模式", text);
    display.text(12, 56, apText(), apOK ? ok : (apFailed ? err : wait));
    display.text(12, 86, "名称: " + AP_SSID, text);
    display.text(12, 116, "密码: " + AP_PASS, text);
    display.text(12, 146, AP_URL, text);
    display.text(12, 176, webText(), webOK ? ok : (webFailed ? err : wait));
    display.text(12, 206, "手机连热点后打开", dim);
    display.text(12, 236, "网页保存后重启", dim);
    display.text(12, 266, "打不开页面?", wait);
    display.text(12, 296, "按住 BOOT 再复位", dim);
    display.text(12, 326, "内存 " + freeKB() + " KB", dim);
    display.flush();
  } catch (drawError) {
    print("draw failed:", drawError);
  }
}

function startAP() {
  try {
    wifi.startAP(AP_SSID, AP_PASS);
    apOK = true;
    apFailed = false;
    print("ap ready:", AP_SSID, AP_URL);
  } catch (apError) {
    apOK = false;
    apFailed = true;
    print("ap failed:", apError);
  }
}

function startWeb() {
  try {
    web.start(WEB_PORT);
    webOK = true;
    webFailed = false;
    print("web ready:", AP_URL, "port", WEB_PORT);
  } catch (webError) {
    webOK = false;
    webFailed = true;
    print("web failed:", webError);
  }
}

function boot() {
  /* The panel is initialized first so AP/web progress stays visible even if
   * the radio or the HTTP server refuses to start. */
  try {
    display.init();
    displayOK = true;
    print("display ready");
  } catch (displayError) {
    displayOK = false;
    print("display failed:", displayError);
  }
  draw();

  startAP();
  draw();

  /* A failed user.js may have left its own server running on another port;
   * stop it so the recovery page always owns WEB_PORT. */
  try {
    web.stop();
  } catch (webStopError) {
    print("web stop failed:", webStopError);
  }
  startWeb();
  draw();

  if (apOK && webOK) print("recovery ready");
  else print("recovery incomplete; watchdog will retry");
}

function retryNetwork() {
  try {
    var apWas = apOK;
    var apFailWas = apFailed;
    var webWas = webOK;
    var webFailWas = webFailed;

    if (!apOK) startAP();
    if (!webOK) {
      startWeb();
    } else {
      try {
        if (!web.ready()) {
          webOK = false;
          print("web stopped; retrying");
        }
      } catch (webReadyError) {
        webOK = false;
        print("web ready check failed:", webReadyError);
      }
    }

    if (apOK !== apWas || apFailed !== apFailWas ||
        webOK !== webWas || webFailed !== webFailWas) {
      draw();
    }
  } catch (retryError) {
    print("retry failed:", retryError);
  }
}

/* A failed user.js may have scheduled timers before it threw.  They would
 * keep drawing, reconnecting, or otherwise fighting this recovery script, so
 * clear every timer slot before scheduling the watchdog. */
try {
  for (var staleTimer = 0; staleTimer < TIMER_SLOTS; staleTimer++) {
    clearTimeout(staleTimer);
  }
} catch (clearError) {
  print("timer clear failed:", clearError);
}

try {
  setInterval(retryNetwork, RETRY_MS);
} catch (timerError) {
  print("watchdog failed:", timerError);
}

/* boot() is already defensive; this last guard keeps the watchdog alive even
 * if an unexpected JS error escapes it. */
try {
  boot();
} catch (bootError) {
  print("boot failed:", bootError);
}
