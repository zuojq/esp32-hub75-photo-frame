#include <Arduino.h>
#include <CWPreferences.h>
#include <DisplayController.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <PictureEngine.h>

// 纯图片显示 + 永久自建热点：
//   1. 开机即开「Clockwise-Foto」热点(固定 http://192.168.4.1/)，密码 = 热点名 Clockwise-Foto
//   2. 拦截 DNS:手机一连上热点,任何网址都解析到 192.168.4.1,
//      Android/iOS 会把它当"需登录的 WiFi"→ 自动弹出上传页(captive portal)
//   3. 若之前真的保存过 WiFi(如家庭路由器)会同时自动连上,局域网也能访问(双模共存)
//   4. 开机 / WiFi 重连 / 按键短按时,屏上显示固定地址 192.168.4.1

static const char *AP_SSID     = "Clockwise-Foto";
static const char *AP_PASSWORD = "Clockwise-Foto";   // 密码 = WiFi 名
static const IPAddress AP_IP(192, 168, 4, 1);        // ESP32 softAP 固定地址,手机就访问它

// ---- captive portal: 把发给我们的 DNS 请求全部答成 192.168.4.1 ----
static WiFiUDP dnsUdp;
static bool dnsUp = false;

static void startCaptiveDns() {
  if (dnsUp) return;
  if (!dnsUdp.begin(53)) {
    Serial.println("[DNS] UDP 53 绑定失败(被占用?)");
    return;
  }
  dnsUp = true;
  Serial.println("[WiFi] DNS 劫持已开:所有域名 → 192.168.4.1(手机连上会自动弹上传页)");
}

static void handleCaptiveDns() {
  if (!dnsUp) return;
  int sz = dnsUdp.parsePacket();
  if (sz <= 0) return;
  uint8_t q[256], r[320];
  int qlen = dnsUdp.read(q, sizeof(q));
  if (qlen < 12 || (q[2] & 0x80)) return;            // 太短或本来就是响应,忽略

  int i = 12;                                        // 跳过问题段:一串 label + 类型/类
  while (i < qlen) {
    uint8_t l = q[i];
    if (l == 0) { i++; break; }
    i += 1 + l;
    if (i >= qlen) return;
  }
  i += 4;                                            // qtype + qclass
  int n = i;

  memcpy(r, q, qlen);
  r[2] = 0x81; r[3] = 0x80;                          // 标准应答,递归可用
  r[6] = 0;    r[7] = 1;                             // ANCOUNT = 1
  // 回答:名字指针指向问题名,类型 A,TTL 60,IP=192.168.4.1
  uint8_t ans[] = {0xC0,0x0C, 0x00,0x01, 0x00,0x01,
                   0x00,0x00,0x00,0x3C, 0x00,0x04,
                   192,168,4,1};
  memcpy(r + n, ans, sizeof(ans));
  dnsUdp.beginPacket(dnsUdp.remoteIP(), dnsUdp.remotePort());
  dnsUdp.write(r, n + (int)sizeof(ans));
  dnsUdp.endPacket();
}

static void startFotoAP() {
  WiFi.mode(WIFI_AP_STA);   // AP(上传入口) 与 STA(自动连已存 WiFi) 共存
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  if (WiFi.softAP(AP_SSID, AP_PASSWORD))   // 密码 = Clockwise-Foto
    Serial.printf("[WiFi] 热点 '%s' 已开(密码同名) → 手机连它后访问 http://%s/\n",
                  AP_SSID, AP_IP.toString().c_str());
  else
    Serial.println("[WiFi] softAP 启动失败!");
  WiFi.setAutoReconnect(true);
  startCaptiveDns();
}

static void joinSavedWifi() {
  ClockwiseParams *p = ClockwiseParams::getInstance();
  p->load();
  if (!p->hasSavedWifi()) {
    Serial.println("[WiFi] 未保存过 WiFi → 仅热点模式(手机连 Clockwise-Foto 用)");
    return;
  }
  Serial.printf("[WiFi] 尝试自动连接已保存网络: %s ...\n", p->wifiSsid.c_str());
  WiFi.begin(p->wifiSsid.c_str(), p->wifiPwd.c_str());
  // 不阻塞等待:AP 已可用;连上与否 loop 里会提示
}

static void showEntry() {
  PictureEngine::getInstance()->showIpOverlay(AP_IP.toString().c_str());
}

void setup() {
  Serial.begin(115200);

  ClockwiseParams::getInstance()->load();
  // 该套件的 64x64 屏为 RBG 面板(绿蓝引脚反),固定开启红绿蓝互换,否则绿色丢失
  ClockwiseParams::getInstance()->swapBlueGreen = true;
  pinMode(2, OUTPUT);
  pinMode(36, INPUT);   // 按键：短按显示固定地址

  DisplayController::getInstance()->begin();
  PictureEngine::getInstance()->begin(DisplayController::getInstance()->getDmaDisplay());

  startFotoAP();      // 永久热点,先开,保证上传入口永远在
  joinSavedWifi();    // 有保存凭据才尝试 STA(可选项)

  PictureEngine::getInstance()->startWebServer();
  showEntry();
}

void loop() {
  handleCaptiveDns();   // 手机一连上热点,把它的域名请求导到 192.168.4.1 → 自动弹页

  // WiFi 状态监视(STA 侧)：连上/重连上时提示一次固定地址
  static bool wasConnected = false;
  bool connected = (WiFi.status() == WL_CONNECTED);
  if (connected && !wasConnected) showEntry();
  wasConnected = connected;

  // 按键(GPIO36,按下为低)：短按显示固定地址 192.168.4.1(松开时触发)
  static bool btnDown = false;
  static bool btnArmed = false;
  static unsigned long btnDownAt = 0;
  bool level = (digitalRead(36) == LOW);
  if (level) {
    if (!btnDown) { btnDown = true; btnDownAt = millis(); }
    else if (!btnArmed && (millis() - btnDownAt) > 40) { btnArmed = true; }
  } else {
    if (btnDown && btnArmed) showEntry();
    btnDown = false;
    btnArmed = false;
  }

  PictureEngine::getInstance()->loop();
}
