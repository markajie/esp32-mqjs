// Round 360x360 clock for Moji 2 / ESP32-C5.
// This runs from /spiffs/user.js; no firmware rebuild is needed.
// Edit WIFI_SSID / WIFI_PASS below and upload this file from the web page.

var WIFI_SSID = "wei-wei-5G";
var WIFI_PASS = "xxxxx";
var NTP_SERVER = "ntp.aliyun.com";
var NTP_FALLBACK = "pool.ntp.org";
var TZ_HOURS = 8;

/* Native RGB565: the driver swaps endianness on flush. */
function rgb(r, g, b) {
  return display.color(r, g, b);
}

var BG    = rgb(0, 0, 0);        // 565 精确黑：低值蓝会被量化成绿，别用 (4,4,7)
var RING = rgb(46, 42, 38);
var RING_MAJOR = rgb(132, 96, 52);
var RING_EDGE = rgb(28, 26, 24);
var ACCENT = rgb(255, 92, 32);
var WHITE = rgb(255, 244, 224);
var AMBER = rgb(255, 164, 48);
var MUTED = rgb(152, 132, 108);
var RED = rgb(255, 64, 48);

var WEEK = ["日", "一", "二", "三", "四", "五", "六"];
var displayOK = false;
var wifiOK = false;
var timeOK = false;
var webOK = false;
var wifiTried = false;
var lastWifiTry = 0;
var lastNtpTry = -60000;
var phase = "boot";

function pad2(n) {
  return n < 10 ? "0" + n : "" + n;
}

/* Gregorian date from days since 1970-01-01 (Howard Hinnant's algorithm). */
function civilFromDays(z) {
  z += 719468;
  var era = Math.floor(z / 146097);
  var doe = z - era * 146097;
  var yoe = Math.floor((doe - Math.floor(doe / 1460) +
                        Math.floor(doe / 36524) -
                        Math.floor(doe / 146096)) / 365);
  var y = yoe + era * 400;
  var doy = doe - (365 * yoe + Math.floor(yoe / 4) - Math.floor(yoe / 100));
  var mp = Math.floor((5 * doy + 2) / 153);
  var d = doy - Math.floor((153 * mp + 2) / 5) + 1;
  var m = mp + (mp < 10 ? 3 : -9);
  y += m <= 2 ? 1 : 0;
  return [y, m, d];
}

function localParts() {
  var ms = system.time();
  if (ms < 1700000000000) {
    return null;
  }
  var total = Math.floor(ms / 1000) + TZ_HOURS * 3600;
  var days = Math.floor(total / 86400);
  var sod = total - days * 86400;
  var date = civilFromDays(days);
  return {
    y: date[0],
    mo: date[1],
    d: date[2],
    h: Math.floor(sod / 3600),
    mi: Math.floor((sod % 3600) / 60),
    s: sod % 60,
    wd: (days + 4) % 7
  };
}

function drawRing(sec) {
  var i;
  for (i = 0; i < 60; i++) {
    var a = i * Math.PI / 30 - Math.PI / 2;
    var major = (i % 5) === 0;
    var r1 = major ? 158 : 162;
    var r2 = 168;
    var c = (i === sec) ? ACCENT : (major ? RING_MAJOR : RING);
    display.line(180 + Math.cos(a) * r1, 180 + Math.sin(a) * r1,
                 180 + Math.cos(a) * r2, 180 + Math.sin(a) * r2, c);
  }
  display.circle(180, 180, 174, RING_EDGE);
  display.circle(180, 180, 153, RING_EDGE);
}

function drawIp() {
  if (!wifiOK) return;
  var ip = wifi.ip();
  if (!ip) return;
  var ipW = display.textWidth(ip);
  var x = Math.floor((360 - ipW) / 2);
  if (x < 8) x = 8;
  display.text(x, 76, ip, MUTED);
}

function drawPrompt(text, sub) {
  if (!displayOK) return;
  display.fillRect(0, 0, 360, 360, BG);
  display.circle(180, 180, 174, RING_EDGE);
  display.circle(180, 180, 153, RING_EDGE);
  display.text(128, 48, "北京时间", AMBER, -1, 1);
  display.text(94, 98, "--:--", WHITE, -1, 4);
  display.text(155, 204, ":--", ACCENT, -1, 2);
  display.text(102, 262, text, MUTED, -1, 1);
  if (sub) display.text(144, 292, sub, RING_MAJOR, -1, 1);
  display.flush();
}

function drawClock(p) {
  if (!displayOK) return;

  /* fillRect only changes the framebuffer. display.fill() would flush a
     blank screen here and again at the end, which causes visible flashing. */
  display.fillRect(0, 0, 360, 360, BG);
  drawRing(p ? p.s : 0);

  display.text(128, 48, "北京时间", AMBER, -1, 1);
  display.circle(180, 36, 3, timeOK ? ACCENT : AMBER);
  drawIp();

  var hh = p ? pad2(p.h) : "--";
  var mm = p ? pad2(p.mi) : "--";
  var ss = p ? pad2(p.s) : "--";

  display.text(54, 98, hh, WHITE, -1, 4);
  display.text(166, 98, ":", ACCENT, -1, 4);
  display.text(194, 98, mm, WHITE, -1, 4);
  display.text(145, 204, ":" + ss, ACCENT, -1, 2);

  if (p) {
    var dateX = 85 + (p.mo < 10 ? 7 : 0) + (p.d < 10 ? 7 : 0);
    display.text(dateX, 262, p.y + "年" + p.mo + "月" + p.d + "日", AMBER, -1, 1);
    display.text(141, 292, "星期" + WEEK[p.wd], MUTED, -1, 1);
  } else if (phase === "wifi") {
    display.text(102, 262, "正在连接 WiFi", MUTED, -1, 1);
    display.text(144, 292, "请稍候", RING_MAJOR, -1, 1);
  } else if (phase === "ntp") {
    display.text(102, 262, "正在校准时间", MUTED, -1, 1);
    display.text(144, 292, "请稍候", RING_MAJOR, -1, 1);
  } else if (phase === "offline") {
    display.text(100, 262, "WiFi 连接失败", RED, -1, 1);
    display.text(141, 292, "自动重试", RING_MAJOR, -1, 1);
  } else {
    display.text(102, 262, "等待网络校时", MUTED, -1, 1);
    display.text(144, 292, "请检查网络", RING_MAJOR, -1, 1);
  }
  display.flush();
}

function render() {
  if (!displayOK) return;
  var p = timeOK ? localParts() : null;
  if (!p) timeOK = false;
  drawClock(p);
}

function ensureWeb() {
  if (webOK) return;
  try {
    web.start(80);
    webOK = true;
    print("clock: web on 80");
  } catch (webError) {
    print("clock: web error", webError);
  }
}

function tryWifi() {
  if (wifiOK && wifi.status().connected) return true;
  wifiOK = false;

  var now = system.uptime();
  if (wifiTried && now - lastWifiTry < 60000) return false;
  wifiTried = true;
  lastWifiTry = now;

  phase = "wifi";
  render();
  print("clock: connecting wifi", WIFI_SSID);
  try {
    wifi.init();
    wifi.connect(WIFI_SSID, WIFI_PASS, 20000);
    wifiOK = wifi.status().connected;
  } catch (wifiError) {
    print("clock: wifi error", wifiError);
    wifiOK = false;
  }
  if (wifiOK) {
    print("clock: wifi ip", wifi.ip());
  } else {
    phase = "offline";
  }
  return wifiOK;
}

function syncNtp() {
  if (!wifiOK || !wifi.status().connected) return false;
  var now = system.uptime();
  if (now - lastNtpTry < 60000) return timeOK;
  lastNtpTry = now;

  phase = "ntp";
  render();
  print("clock: ntp sync", NTP_SERVER);
  try {
    timeOK = net.sntp(NTP_SERVER);
  } catch (ntpError) {
    print("clock: ntp error", ntpError);
    timeOK = false;
  }
  if (!timeOK) {
    try {
      timeOK = net.sntp(NTP_FALLBACK);
    } catch (ntpFallbackError) {
      print("clock: ntp fallback error", ntpFallbackError);
      timeOK = false;
    }
  }
  if (!timeOK) {
    print("clock: ntp failed");
  } else {
    print("clock: ntp ok");
  }
  return timeOK;
}

function tick() {
  if (wifiOK && !wifi.status().connected) {
    wifiOK = false;
  }
  if (!wifiOK) {
    if (tryWifi()) {
      ensureWeb();
      syncNtp();
    }
  } else if (!timeOK) {
    syncNtp();
  }
  render();
}

function start() {
  try {
    display.init();
    displayOK = true;
    print("clock: display", display.width(), display.height());
  } catch (displayError) {
    print("clock: display error", displayError);
  }

  phase = "wifi";
  drawPrompt("正在连接 WiFi", "请稍候");

  tryWifi();
  ensureWeb();

  if (wifiOK) {
    syncNtp();
  }

  render();
  setInterval(tick, 1000);
  print("clock: ready");
}

start();