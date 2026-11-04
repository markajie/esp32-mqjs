# MQuickJS 功能示例

本文是这块 ESP32-C5 / Moji 2 固件里 JavaScript 模块的使用手册。所有示例都是
MQuickJS 的 ES5 子集，不使用 let、const、箭头函数或 async/await。

## 目录

1. 运行方式
2. 最小脚本与系统信息
3. WiFi
4. MQTT
5. WebSocket
6. HTTP
7. 音频播放与录音
8. BLE
9. 文件系统
10. 显示中文
11. 定时器与事件
12. 常见问题

## 1. 运行方式

### 通过网页修改（推荐）

没有 /spiffs/user.js，或它运行出错时，固件会运行内嵌的 main/js/main.js。
这个恢复脚本不需要联网：它直接开热点并启动网页，屏幕上会显示：

~~~text
SSID:     Moji2-C5
密码:     12345678
地址:     http://192.168.4.1/
~~~

这个恢复脚本只开热点，不连路由器。需要连现有 WiFi 的应用写在 user.js 里
（见第 3 节 wifi.connect）。手机连上热点后打开地址即可。如果 user.js 自己
调用了 web.start(80)，则直接打开：

~~~text
http://<板子 IP>/
~~~

编辑后点击 **Save & reboot**，内容会写入 **/spiffs/user.js**，重启后优先运行。
点击 **Delete user.js & reboot** 会删除用户脚本，回到固件内嵌的 main/js/main.js。
整个过程不需要重新编译固件。

网页没有登录认证，只适合可信局域网。

### 恢复模式

如果 user.js 死循环或把 WiFi 配置写错，按住 **BOOT** 再复位。固件会跳过
/spiffs/user.js 这一次启动，直接运行内嵌 main.js，网页会重新可用。

user.js 在启动时抛出异常也会自动退回内嵌 main.js，并清掉它留下的定时器，
所以不需要先擦除 flash。

### 修改内嵌脚本

内嵌 main.js 在 main/js/main.js，修改后需要：

~~~powershell
idf.py build
idf.py -p COMx flash monitor
~~~

## 2. 最小脚本与系统信息

~~~js
print("chip:", system.chip());
print("mac:", system.mac());
print("free heap:", system.freeHeap());
print("free psram:", system.freePsram());
print("uptime ms:", system.uptime());
print("board:", board.name, board.width, board.height);
~~~

常用函数：

- system.chip() / system.mac() / system.freeHeap()
- system.uptime() / system.delay(ms) / system.reboot()
- system.time() / system.setTime(ms)
- system.log(text)

## 3. WiFi

wifi.connect 是阻塞调用：等到拿到 IP 或超时后返回；失败会抛异常。

~~~js
try {
  wifi.init();
  wifi.connect("wei-wei-5G", "xxxxxxx", 20000);

  var st = wifi.status();
  print("connected:", st.connected);
  print("ssid:", st.ssid);
  print("ip:", st.ip);
  print("rssi:", st.rssi);
} catch (wifiError) {
  print("wifi failed:", wifiError);
}
~~~

扫描周围 AP：

~~~js
try {
  var list = wifi.scan();
  for (var i = 0; i < list.length; i++) {
    print(list[i].ssid, list[i].rssi, "ch", list[i].channel);
  }
} catch (scanError) {
  print("scan failed:", scanError);
}
~~~

也可以开热点：

~~~js
wifi.startAP("Moji2-C5", "12345678");
print("AP started; default gateway is usually 192.168.4.1");
~~~

注意：

- 先连接 WiFi，再调用 net / mqtt / ws / web。
- 5GHz 和 2.4GHz 由固件的 WIFI_BAND_MODE_AUTO 自动处理。
- 连接失败时检查 SSID、密码、5G 信道和信号强度。

## 4. MQTT

MQTT 是独立全局对象 mqtt，和 net 的 HTTP 接口不是一回事。

### 完整示例

~~~js
var WIFI_SSID = "wei-wei-5G";
var WIFI_PASS = "xxxxxxx";

try {
  wifi.init();
  wifi.connect(WIFI_SSID, WIFI_PASS, 20000);
  print("wifi ip:", wifi.ip());
} catch (wifiError) {
  print("wifi failed:", wifiError);
}

mqtt.onConnect(function () {
  print("mqtt connected");
  mqtt.subscribe("c5/demo", 0);
  mqtt.publish("c5/demo", "hello from c5", 0);
});

mqtt.onMessage(function (topic, payload) {
  print("mqtt msg:", topic, payload);
});

mqtt.onDisconnect(function () {
  print("mqtt disconnected");
});

try {
  mqtt.connect("mqtt://broker.emqx.io:1883", "c5-" + system.mac());
} catch (mqttError) {
  print("mqtt start failed:", mqttError);
}

try {
  web.start(80);
  print("web:", "http://" + wifi.ip() + "/");
} catch (webError) {
  print("web failed:", webError);
}
~~~

### TLS

把 URL 改成 mqtts://，端口通常是 8883：

~~~js
mqtt.connect("mqtts://broker.example.com:8883", "c5-demo");
~~~

TLS 使用 ESP-IDF 的 CA 证书 bundle。

### API

- mqtt.connect(url, clientId?, username?, password?)
- mqtt.publish(topic, payload, qos?, retain?)
- mqtt.subscribe(topic, qos?)
- mqtt.onConnect(fn) / mqtt.onMessage(fn) / mqtt.onDisconnect(fn)
- mqtt.connected() / mqtt.disconnect()

注意：

- connect 返回 true 只代表“已启动”，不代表已经连上；订阅和发布放在
  onConnect 回调里最稳。
- 回调参数顺序是 **(topic, payload)**。
- 单条消息最多 8KB，超过会被截断。
- 客户端 ID 要确保唯一，建议带 MAC 或设备编号。
- 回调在 JS 任务里执行，不要在回调里做长时间阻塞。

## 5. WebSocket

~~~js
try {
  wifi.connect("wei-wei-5G", "xxxxxxx", 20000);
} catch (wifiError) {
  print("wifi failed:", wifiError);
}

ws.onConnect(function () {
  print("ws connected");
  ws.send("hello ws");
});

ws.onMessage(function (data) {
  print("ws msg:", data);
});

ws.onDisconnect(function () {
  print("ws disconnected");
});

try {
  ws.connect("wss://ws.postman-echo.com/raw");
} catch (wsError) {
  print("ws start failed:", wsError);
}
~~~

API：

- ws.connect(url) / ws.send(text) / ws.sendBinary(data)
- ws.onConnect(fn) / ws.onMessage(fn) / ws.onDisconnect(fn)
- ws.connected() / ws.close()

ws:// 和 wss:// 都支持，wss 使用证书 bundle。单帧最多 8KB，分片帧不会自动拼接。

## 6. HTTP

net 是 HTTP 客户端，适合 REST 接口、下载小文件、上报状态。

~~~js
try {
  var r = net.get("https://httpbin.org/get", "Accept: application/json", 15000);
  print("status:", r.status);
  print("ok:", r.ok);
  print("body:", r.body);
} catch (httpError) {
  print("http failed:", httpError);
}
~~~

POST：

~~~js
try {
  var r2 = net.post(
    "https://httpbin.org/post",
    "{\"value\":123}",
    "application/json",
    "X-Device: c5"
  );
  print(r2.status, r2.body);
} catch (postError) {
  print("post failed:", postError);
}
~~~

通用请求：

~~~js
var r3 = net.request({
  url: "https://example.com/api",
  method: "PUT",
  body: "hello",
  contentType: "text/plain",
  headers: "Authorization: Bearer token",
  timeout: 15000
});
~~~

响应对象：

- status：HTTP 状态码
- ok：状态码是否 2xx
- body：响应文本
- truncated：只有响应过大时才可能出现

响应体上限约 128KB。headers 是换行分隔的字符串，例如：

~~~js
var headers = "Authorization: Bearer x\nAccept: application/json";
~~~

## 7. 音频播放与录音

音频硬件是 ES8311 + I2S，采样率 24kHz，双声道播放；录音返回单声道平均值。

### 初始化、播放和音量

~~~js
try {
  audio.init();
  print("audio ready:", audio.ready());

  audio.setVolume(60);
  audio.tone(880, 120, 30);
  // Upload an OGG to /spiffs first; missing files raise an error.
  audio.playOgg("/spiffs/welcome.ogg");
} catch (audioError) {
  print("audio failed:", audioError);
}
~~~

playOgg 会解码 OGG/Opus；`path` 必填，文件不存在会抛错。固件不再内嵌
welcome.ogg；需要开机音就自己把 OGG 上传到 `/spiffs/welcome.ogg`。

直接播放 PCM 数组：

~~~js
var samples = [];
for (var i = 0; i < 2400; i++) {
  samples.push(Math.sin(i / 10) * 8000);
}
audio.play(samples);
~~~

第二个参数为 true 时按立体声解释。

### 录音

音频录音的 JS 方法是 **audio.record(ms)**，不是单独的文件：

~~~js
try {
  audio.init();

  var pcm = audio.record(500);   // 500ms
  print("samples:", pcm.length); // 24000 * 0.5 = 12000

  var peak = 0;
  var sum = 0;
  for (var i = 0; i < pcm.length; i++) {
    var v = pcm[i];
    if (v < 0) v = -v;
    if (v > peak) peak = v;
    sum += v;
  }
  print("peak:", peak);
  print("average:", sum / pcm.length);
} catch (recordError) {
  print("record failed:", recordError);
}
~~~

返回值是 **signed 16-bit 单声道样本数组**，采样率 24kHz。左右声道会取平均。
record 是阻塞调用，ms 范围 10..5000。

### 保存为 raw PCM

fs.write 接受字节数组。把 16-bit 样本拆成小端字节即可：

~~~js
try {
  audio.init();
  var pcm = audio.record(250); // 6000 样本
  var bytes = [];

  for (var i = 0; i < pcm.length; i++) {
    var v = pcm[i] & 0xFFFF;
    bytes.push(v & 0xFF);
    bytes.push((v >> 8) & 0xFF);
  }

  fs.write("/spiffs/record.pcm", bytes);
  print("saved", bytes.length, "bytes");
} catch (saveError) {
  print("save failed:", saveError);
}
~~~

这是 24kHz、单声道、16-bit little-endian 的裸 PCM，没有 WAV 头。

### 录音注意事项

- 单次最长 5000ms，但 5 秒会产生 120000 个 JS 数值，容易耗尽 512KB JS 堆。
  实际建议 250ms 到 1000ms 分片记录。
- 录音和播放共用 I2S，录音期间不要同时播放。
- 如果 peak 一直是 32768 或 0，检查麦克风增益、静音状态和硬件连接。
- record 目前不直接生成 WAV/MP3，需要应用层打包或上传 raw PCM。

### 完整 user.js：WiFi + Web + 录音回放

下面这份脚本可以直接粘贴到网页编辑器，点 **Save & reboot** 后运行。它把
WiFi、网页和录音测试放在一起：

- 先尝试连接顶部的 `WIFI_SSID` / `WIFI_PASS`，超时 6 秒。
- 连接失败会自动开热点 `Moji2-C5` / `12345678`，网页地址是
  `http://192.168.4.1/`。这样即使路由器密码写错，也能连上热点改脚本，
  不需要重新编译固件。
- 连接成功时网页地址是 `http://<板子 IP>/`，屏幕和串口都会显示。
- 网络和网页就绪后 8 秒录 500ms 并立即播放，之后每 15 秒重复一次；串口打印
  样本数、峰值和平均绝对值。
- `REPEAT_MS = 0` 改为只测一次；这样每次 Save & reboot 都会在 8 秒后测一次。
  `RECORD_MS` 建议 250–1000，不要超过 1000。
- 网页本身是代码上传页，不是录音按钮；录音由脚本的定时器触发。
- 如果板子连上了你不在的那个局域网，网页在板子 IP 上；按住 BOOT 复位会跳过
  user.js，回到 main.js 的 `Moji2-C5` 热点，仍然可以改脚本。

WiFi 连接放在 `setTimeout(..., 0)` 里执行，而不是直接写在顶层，是因为引擎
给一次 JS 求值 10 秒上限；这样 6 秒连接超时 + 开热点仍能完成，不会把整个
user.js 判超时。连接失败后 `afterNetwork` 再单独启动网页，进一步留出余量。

~~~js
/*
 * user.js: WiFi + web + record/play test.
 * Copy this whole file into the web editor and press "Save & reboot".
 * If the WiFi below is wrong, the board starts its own AP so the page is
 * still reachable at http://192.168.4.1/.
 */

var WIFI_SSID = "xxxxx";
var WIFI_PASS = "xxxxxx";
var WIFI_TIMEOUT_MS = 6000; // keep below the 10 s eval budget

var AP_SSID = "Moji2-C5";
var AP_PASS = "12345678";
var AP_URL = "http://192.168.4.1/";

var WEB_PORT = 80;
var RECORD_MS = 500;
var PLAY_VOLUME = 75;
var FIRST_TEST_MS = 8000;   // after web is ready
var REPEAT_MS = 15000;      // 0 = only one test

var online = false;
var apMode = false;
var webOK = false;
var displayOK = false;
var audioOK = false;
var busy = false;
var pageURL = "";

function show(a, b, c, d, e, f) {
  try {
    if (!displayOK) {
      display.init();
      displayOK = true;
    }
    var w = display.width();
    var h = display.height();
    display.fillRect(0, 0, w, h, display.color(8, 8, 8));
    var lines = [a, b, c, d, e, f];
    var y = 24;
    for (var i = 0; i < lines.length; i++) {
      if (lines[i]) {
        display.text(14, y, lines[i], display.color(235, 235, 235));
        y += 40;
      }
    }
    display.flush();
  } catch (showError) {
    print("display:", showError);
  }
}

function drawStatus(status) {
  var l1 = "录音回放测试";
  var l2, l3, l4, l5, l6;

  if (online) {
    l2 = "WiFi: " + WIFI_SSID;
    l3 = pageURL ? pageURL : "IP 未获取";
    l4 = "网页: " + (webOK ? "已开启" : "启动失败");
    l5 = status;
    l6 = REPEAT_MS > 0 ? ("每 " + Math.round(REPEAT_MS / 1000) + "s 重复") : "单次测试";
  } else if (apMode) {
    l2 = "热点: " + AP_SSID;
    l3 = "密码: " + AP_PASS;
    l4 = AP_URL;
    l5 = "网页: " + (webOK ? "已开启" : "启动失败");
    l6 = status;
  } else {
    l2 = "WiFi/热点启动失败";
    l3 = "网页: " + (webOK ? "已开启" : "失败");
    l4 = "请串口查看日志";
    l5 = status;
    l6 = "";
  }
  show(l1, l2, l3, l4, l5, l6);
}

function connectNetwork() {
  show("录音回放测试", "连接 WiFi...", WIFI_SSID);

  try {
    wifi.init();
    wifi.connect(WIFI_SSID, WIFI_PASS, WIFI_TIMEOUT_MS);
    online = true;

    var ip = "";
    try {
      ip = wifi.ip();
    } catch (ipError) {
      print("ip:", ipError);
    }
    if (!ip) {
      try {
        ip = wifi.status().ip;
      } catch (statusError) {
        print("status:", statusError);
      }
    }
    pageURL = ip ? ("http://" + ip + "/") : "";
    print("wifi ok:", WIFI_SSID, pageURL);
  } catch (wifiError) {
    print("wifi failed:", wifiError);
    try {
      wifi.startAP(AP_SSID, AP_PASS);
      apMode = true;
      pageURL = AP_URL;
      print("AP fallback:", AP_SSID, AP_URL);
    } catch (apError) {
      print("AP failed:", apError);
      pageURL = "";
    }
  }

  setTimeout(afterNetwork, 0);
}

function startWeb() {
  try {
    web.stop();
  } catch (webStopError) {
    print("web stop:", webStopError);
  }

  try {
    web.start(WEB_PORT);
    webOK = true;
    print("web:", pageURL ? pageURL : ("port " + WEB_PORT));
  } catch (webError) {
    webOK = false;
    print("web failed:", webError);
  }
}

function afterNetwork() {
  startWeb();
  drawStatus("录音 " + RECORD_MS + "ms");
  schedule();
  if (pageURL) {
    print("open:", pageURL);
  } else {
    print("web port", WEB_PORT, "started; no URL");
  }
}

function recordAndPlay() {
  if (busy) {
    return;
  }
  busy = true;

  try {
    if (!audioOK) {
      audio.init();
      audioOK = true;
    }
    audio.setVolume(PLAY_VOLUME);

    drawStatus("请说话 " + RECORD_MS + "ms");
    var pcm = audio.record(RECORD_MS);
    if (!pcm || pcm.length < 1) {
      throw "record returned no samples";
    }

    var peak = 0;
    var sum = 0;
    for (var i = 0; i < pcm.length; i++) {
      var v = pcm[i];
      if (v < 0) {
        v = -v;
      }
      if (v > peak) {
        peak = v;
      }
      sum += v;
    }
    var avg = Math.round(sum / pcm.length);
    print("record:", pcm.length, "samples, peak:", peak, "avg:", avg);

    drawStatus("峰值 " + peak + "  播放中...");
    var played = audio.play(pcm);
    print("play:", played, "ms");
    pcm = null;

    drawStatus("完成 峰值 " + peak);
  } catch (recordError) {
    audioOK = false;
    print("record/play failed:", recordError);
    drawStatus("录音/播放失败");
  }

  busy = false;
}

function schedule() {
  try {
    setTimeout(function () {
      try {
        recordAndPlay();
        if (REPEAT_MS > 0) {
          setInterval(recordAndPlay, REPEAT_MS);
        }
      } catch (startError) {
        print("test timer:", startError);
      }
    }, FIRST_TEST_MS);
  } catch (timerError) {
    print("timer failed:", timerError);
  }
}

function boot() {
  print("user.js: WiFi + web + record/play test");
  setTimeout(connectNetwork, 0);
}

try {
  boot();
} catch (bootError) {
  print("boot failed:", bootError);
}
~~~

串口正常输出大致是：

~~~text
user.js: WiFi + web + record/play test
wifi ok: wei-wei-5G http://192.168.1.23/
web: http://192.168.1.23/
open: http://192.168.1.23/
record: 12000 samples, peak: 12345 avg: 2345
play: 500 ms
~~~

如果 `peak` 一直是 0，检查麦克风是否接好、是否被静音；如果接近 32768，
说明输入削波，离麦克风远一点或降低增益。录音和播放共用 I2S，测试期间不要
同时做其他音频操作。

## 8. BLE

BLE 是 NimBLE 的 UART 风格 GATT 服务。

~~~js
try {
  ble.init("Moji2-C5");
  print("ble:", ble.name(), ble.address());

  ble.onReceive(function (data) {
    print("ble rx:", data);
    if (ble.connected()) {
      ble.send("ok");
    }
  });
} catch (bleError) {
  print("ble failed:", bleError);
}
~~~

API：

- ble.init(name?) / ble.name(name?)
- ble.address() / ble.connected()
- ble.send(text) / ble.onReceive(fn) / ble.stop()

接收数据是二进制字符串，单包最多 512 字节。

## 9. 文件系统

SPIFFS 路径都带 /spiffs/ 前缀。

~~~js
fs.write("/spiffs/test.txt", "hello");
print(fs.read("/spiffs/test.txt"));
print(fs.exists("/spiffs/test.txt"));
print(fs.size("/spiffs/test.txt"));

var files = fs.list("/spiffs");
for (var i = 0; i < files.length; i++) {
  print(files[i]);
}

fs.remove("/spiffs/test.txt");
~~~

API：

- fs.exists / fs.read / fs.readBytes / fs.write
- fs.remove / fs.rename / fs.size / fs.list / fs.info

fs.write 的第二个参数可以是字符串，也可以是字节数组。

## 10. 显示中文

~~~js
try {
  display.init();
  display.fill(display.color(0, 0, 0));

  var w = display.text(16, 20, "中文 English 123", 0xFFFF);
  print("text width:", w);

  display.text(16, 60, "ESP32-C5", 0xFFFF);
  display.flush();
} catch (displayError) {
  print("display failed:", displayError);
}
~~~

说明：

- `display.text` 只有一套 24px Noto Sans SC 字库，英文、数字和中文共享字重与基线。
- 字库有 3682 个字符；超出范围的汉字显示为替代框。
- 颜色一律使用 display.color(r, g, b) 的原生 RGB565 值；字节序转换在
  display 模块的 DMA staging 内完成，JS 侧不要自行交换字节或取反。
- 面板、QSPI 引脚、背光 PWM 都由 esp_board_manager 读取板级 YAML 与
  setup_device.c 完成；显示模块只维护 PSRAM 帧缓冲，不做任何面板级微调。
- 大号数字（scale 2/4）使用 96px 抗锯齿字模：scale 4 为 1:1，scale 2 为
  2x2 降采样，因此时钟不会出现锯齿。

## 11. 定时器与事件

~~~js
var count = 0;

var timer = setInterval(function () {
  count++;
  print("tick", count);
  if (count >= 5) {
    clearInterval(timer);
  }
}, 1000);

setTimeout(function () {
  print("one shot");
}, 500);
~~~

MQTT、WebSocket、BLE 的回调也由同一个 JS 事件循环调度。不要写死循环，
否则网页和回调都会停止响应。

## 12. 常见问题

### 为什么 mqtt.connect 报 WiFi is not connected？

先调用 wifi.connect 并确认 wifi.status().connected 为 true，再启动 MQTT 或 WebSocket。

### 为什么 catch 写多次会报 catch variable already exists？

MQuickJS 对同一作用域内的 catch 变量名比较严格。每次 catch 用不同的名字：

~~~js
try { ... } catch (wifiError) { ... }
try { ... } catch (mqttError) { ... }
try { ... } catch (webError) { ... }
~~~

### 为什么网页打不开？

- 脚本没有调用 web.start(80)
- WiFi 没连上，或 IP 变了
- 脚本死循环，按住 BOOT 复位后重新上传
- 没有 user.js 或 user.js 报错时，先连恢复热点 Moji2-C5（密码 12345678），
  再打开 http://192.168.4.1/

### 为什么录音数组很大？

record 返回 JS 数值数组，每个样本都占 JS 堆。不要一次录 5 秒；用 250ms 到
1000ms 分片，或修改固件增加直接写文件的接口。

### 为什么 MQTT/WS 消息被截断？

当前每条回调最多 8KB。应用层应分片发送，或修改对应模块的缓冲区上限。
