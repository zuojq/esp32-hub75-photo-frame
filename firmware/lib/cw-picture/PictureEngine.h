// 纯图片显示引擎：手机上传 PNG/GIF → 解码 → 缩放到 64x64 全屏显示
#pragma once

#include <Arduino.h>
#include <SPIFFS.h>
#include <Adafruit_GFX.h>
#include <WebServer.h>
#include <WiFi.h>

#include "CWPreferences.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <AnimatedGIF.h>
// 三个解码库各自 #define 了这些字节序/寄存器宏，逐个包含前清掉避免重复定义
#undef INTELSHORT
#undef INTELLONG
#undef REGISTER_WIDTH
#undef BIGINT
#undef BIGUINT
#undef ALLOWS_UNALIGNED
#include <PNGdec.h>
#undef INTELSHORT
#undef INTELLONG
#undef REGISTER_WIDTH
#undef BIGINT
#undef BIGUINT
#undef ALLOWS_UNALIGNED
#include <JPEGDEC.h>

#define PIC_PATH  "/pic.bin"
#define DRAW_PATH "/pic_draw.bin"     // 绘画作品(64x64 RGB565,与图片互斥)
#define PIC_LIMIT_BYTES  (600 * 1024)  // 上传上限 600KB

// ---- 临时四色诊断图(白/红/绿/蓝) ----
#define PIC_COLOR_TEST 0
static const uint8_t PIC_COLOR_TEST_IMG[] = {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,64,0,0,0,64,8,2,0,0,0,37,11,230,137,0,0,0,85,73,68,65,84,120,218,237,208,177,17,0,48,12,2,49,246,95,218,153,193,5,133,115,226,126,1,145,105,47,169,22,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,128,53,32,253,139,142,7,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,240,29,224,1,181,222,233,90,49,249,136,98,0,0,0,0,73,69,78,68,174,66,96,130};

struct PictureEngine
{
  Adafruit_GFX *_gfx = nullptr;
  MatrixPanel_I2S_DMA *_panel = nullptr;
  uint8_t _bright = 128;
  uint16_t _fb[64 * 64];            // 64x64 RGB565 帧缓冲
  uint8_t  _fbDirty = 0;
  bool _showIp = false;
  unsigned long _ipUntil = 0;

  enum Kind { K_NONE, K_PNG, K_JPG, K_GIF, K_DRAW } _kind = K_NONE;
  // 绘画调色板(16色,索引 0=黑背景,和 /paint 网页里的顺序一致)
  static const uint16_t DRAW_PAL[16];
  bool _gifOpen = false;
  AnimatedGIF _gif;     // 解码器对象较大，放全局(BSS)避免栈溢出
  PNG _pngDec;
  JPEGDEC _jpgDec;
  uint8_t *_gifBuf = nullptr;   // GIF 整段读入内存再解码(文件流回调不可靠)
  size_t _gifBufLen = 0;
  uint32_t _canvasW = 0, _canvasH = 0;
  unsigned long _nextMs = 0;

  WebServer *_srv = nullptr;
  String _lastStatus;

  static PictureEngine *instance;

  static PictureEngine *getInstance() {
    static PictureEngine eng;
    instance = &eng;
    return &eng;
  }

  // ---------- 工具 ----------
  void clearFb() { memset(_fb, 0, sizeof(_fb)); _fbDirty = 1; }

  void paint() {
    if (!_fbDirty || !_gfx) return;
    _fbDirty = 0;
    _gfx->drawRGBBitmap(0, 0, _fb, 64, 64);
  }

  // 状态文字(如 IP)可能盖在图片上，调用它把当前图片重新画回屏幕
  void refresh() {
    if (_kind == K_NONE) { clearFb(); }
    _fbDirty = 1;
    paint();
  }

  // 显示 IP 几秒后自动回到图片(两行,自动按点号分行)
  void showIpOverlay(const char *ip) {
    if (!_gfx) return;
    _gfx->fillScreen(0);
    _gfx->setTextColor(0xFFFF);
    _gfx->setTextSize(1);
    String s(ip);
    int d1 = s.indexOf('.');
    int d2 = s.indexOf('.', d1 + 1);
    String l1 = d2 > 0 ? s.substring(0, d2) : s;   // 例如 192.168
    String l2 = d2 > 0 ? s.substring(d2 + 1) : ""; // 例如 1.23
    _gfx->setCursor(0, 6);
    _gfx->println(l1);
    _gfx->setCursor(0, 14);
    _gfx->println(l2);
    _showIp = true;
    _ipUntil = millis() + 4000;
  }

  // 将「宽 w 高 h」原图的第 srcRow 行整行(rgb565, w 个像素)按行区间法写入 64x64
  // band = [floor(row*64/h), ceil((row+1)*64/h))
  void putScaledRow(const uint16_t *row, int w, int h, int srcRow) {
    if (w <= 0 || h <= 0) return;
    int a = (int)(((int64_t)srcRow * 64) / h);
    int64_t num = (int64_t)(srcRow + 1) * 64;
    int b = (int)((num + h - 1) / h);
    if (b > 64) b = 64;
    for (int dy = a; dy < b; dy++) {
      uint16_t *dst = &_fb[dy * 64];
      for (int dx = 0; dx < 64; dx++) {
        int64_t sc = ((int64_t)dx * w + w / 2) / 64;
        if (sc < 0) sc = 0; else if (sc >= w) sc = w - 1;
        dst[dx] = row[sc];
      }
    }
  }

  // 区域映射版本(逐行带宽平均近似) -> 简单起见用 putScaledRow 即可
  // ---------- 显示 ----------
  void begin(MatrixPanel_I2S_DMA *panel) {
    _gfx = panel;
    _panel = panel;
    ClockwiseParams::getInstance()->load();
    _bright = (uint8_t)constrain(ClockwiseParams::getInstance()->displayBright, 0, 255);
    if (_panel) _panel->setBrightness8(_bright);
    if (!SPIFFS.begin(true)) {
      Serial.println("[pic] SPIFFS mount failed");
    }
#if PIC_COLOR_TEST
    {
      File w = SPIFFS.open(PIC_PATH, "w");
      if (w) { w.write(PIC_COLOR_TEST_IMG, sizeof(PIC_COLOR_TEST_IMG)); w.close(); }
      Serial.println("[pic] color test written");
    }
#endif
    clearFb();
    // 图片与绘画互斥：有图片显图片,否则显上次绘画(最后一个动作覆盖另一个)
    if (SPIFFS.exists(PIC_PATH)) setImageFile(PIC_PATH);
    else if (SPIFFS.exists(DRAW_PATH)) loadDrawFile();
    else { _kind = K_NONE; _lastStatus = "还没有内容,可上传图片或去绘画"; clearFb(); paint(); }
  }

  // 亮度 0-255，立即生效并保存(NVS)，重启保持
  void setBrightness(int v) {
    v = constrain(v, 0, 255);
    _bright = (uint8_t)v;
    if (_panel) _panel->setBrightness8(_bright);
    ClockwiseParams::getInstance()->load();
    ClockwiseParams::getInstance()->displayBright = (uint8_t)v;
    ClockwiseParams::getInstance()->save();
    Serial.printf("[pic] brightness %d\n", v);
  }
  int getBrightness() { return _bright; }

  // 探测文件头并开始显示
  void setImageFile(const char *path) {
    if (_gifOpen) { _gif.close(); _gifOpen = false; }
    freeGifBuf();

    File f = SPIFFS.open(path, FILE_READ);
    if (!f) { _kind = K_NONE; _lastStatus = "没有图片(可先上传一张)"; clearFb(); paint(); return; }
    uint8_t head[16] = {0};
    size_t n = f.read(head, sizeof(head));
    f.close();
    if (n < 6) { _kind = K_NONE; _lastStatus = "文件太短/损坏"; clearFb(); paint(); return; }

    if (head[0] == 'G' && head[1] == 'I' && head[2] == 'F') {
      // GIF 尺寸过大时解码器会崩,先读头部尺寸做保护
      uint16_t gw = head[6] | (head[7] << 8);
      uint16_t gh = head[8] | (head[9] << 8);
      if (gw == 0 || gh == 0 || gw > 480 || gh > 480) {
        Serial.printf("[pic] gif too big %ux%u(需<=480)\n", gw, gh);
        _kind = K_NONE;
        _lastStatus = "GIF 过大: " + String(gw) + "x" + String(gh) + "，需<=480x480";
        clearFb();
        return;
      }
      _kind = K_GIF;
      if (openGif(path)) {
        _lastStatus = "OK: GIF " + String((int)_canvasW) + "x" + String((int)_canvasH);
        clearFb();
        _nextMs = millis();
        playOneGifFrame();   // 立刻显示第一帧
      } else {
        _kind = K_NONE;
        _lastStatus = "GIF 打开失败(格式/损坏?)";
      }
    } else if (head[0] == 0x89 && head[1] == 'P' && head[2] == 'N' && head[3] == 'G') {
      _kind = K_PNG;
      showStaticPng(path);
    } else if (head[0] == 0xFF && head[1] == 0xD8 && head[2] == 0xFF) {
      _kind = K_JPG;
      showStaticJpg(path);
    } else {
      _kind = K_NONE;
      Serial.println("[pic] unsupported format");
      _lastStatus = "不支持的文件(用 PNG / JPG / GIF)";
    }
    paint();
  }

  // ---------- 静态 PNG ----------
  // 解码整帧到 RGB565 后逐行缩放(简单、鲁棒)
  void showStaticPng(const char *path) {
    File f = SPIFFS.open(path, FILE_READ);
    if (!f) { _kind = K_NONE; return; }
    size_t sz = f.size();
    if (sz == 0 || sz > PIC_LIMIT_BYTES) { f.close(); _kind = K_NONE; return; }
    uint8_t *buf = (uint8_t *)malloc(sz);
    if (!buf) { f.close(); _kind = K_NONE; Serial.println("[pic] png oom"); return; }
    f.read(buf, sz);
    f.close();

    if (_pngDec.openRAM(buf, (int)sz, staticPngRowCb) != 0) {
      free(buf); _kind = K_NONE; Serial.println("[pic] png open fail"); return;
    }
    int W = _pngDec.getWidth(), H = _pngDec.getHeight();
    if (W <= 0 || H <= 0 || W > 1024 || H > 1024) {
      _pngDec.close();
      free(buf); _kind = K_NONE; Serial.println("[pic] png too big"); return;
    }
    _pngW = W; _pngH = H;
    _pngRowBuf = (uint16_t *)malloc(W * 2);
    if (!_pngRowBuf) { _pngDec.close(); free(buf); _kind = K_NONE; Serial.println("[pic] png row oom"); return; }
    _png = &_pngDec;
    clearFb();
    int rc = _pngDec.decode(this, 0);
    _png = nullptr;
    _pngDec.close();
    free(_pngRowBuf); _pngRowBuf = nullptr;
    free(buf);
    if (rc != 0) { _kind = K_NONE; Serial.printf("[pic] png decode fail %d\n", rc); return; }
    Serial.printf("[pic] png shown %dx%d\n", W, H);
    _lastStatus = "OK: PNG " + String(W) + "x" + String(H);
  }

  static PNG *_png;
  static uint16_t *_pngRowBuf;
  static int _pngW, _pngH;

  static void staticPngRowCb(PNGDRAW *d) {
    PictureEngine *e = instance;
    if (!e || !_png || !_pngRowBuf) return;
    _png->getLineAsRGB565(d, _pngRowBuf, PNG_RGB565_LITTLE_ENDIAN, -1);
    e->putScaledRow(_pngRowBuf, _pngW, _pngH, d->y);
  }

  // ---------- 静态 JPG ----------
  void showStaticJpg(const char *path) {
    File f = SPIFFS.open(path, FILE_READ);
    if (!f) { _kind = K_NONE; return; }
    _jpgDec.close();
    // 注意：JPEGDEC 成功返回 1(与 PNG 的 0 不同)
    int orc = _jpgDec.open(f, staticJpgCb);
    if (orc <= 0) {
      f.close(); _kind = K_NONE;
      Serial.printf("[pic] jpg open fail (%d)\n", orc);
      return;
    }
    int W = _jpgDec.getWidth(), H = _jpgDec.getHeight();
    if (W <= 0 || H <= 0 || W > 1024 || H > 1024) {
      _jpgDec.close(); _kind = K_NONE; Serial.println("[pic] jpg too big"); return;
    }
    _jpgW = W; _jpgH = H;
    _jpgDec.setPixelType(RGB565_LITTLE_ENDIAN);
    clearFb();
    int rc = _jpgDec.decode(0, 0, JPEG_LE_PIXELS);
    _jpgDec.close();  // 内部会关闭文件
    if (rc <= 0) { _kind = K_NONE; Serial.printf("[pic] jpg decode fail %d\n", rc); return; }
    Serial.printf("[pic] jpg shown %dx%d\n", W, H);
    _lastStatus = "OK: JPG " + String(W) + "x" + String(H);
  }

  static int _jpgW, _jpgH;

  static int staticJpgCb(JPEGDRAW *d) {
    PictureEngine *e = instance;
    if (!e) return 1;
    uint16_t *px = d->pPixels;
    int stride = d->iWidth;
    int rows = d->iHeight;
    if (!px || rows <= 0) return 1;
    for (int j = 0; j < rows; j++) {
      int ySrc = d->y + j;
      if (ySrc < 0 || ySrc >= _jpgH) continue;
      int dy = (int)(((int64_t)ySrc * 64 + _jpgH / 2) / _jpgH);
      if (dy < 0 || dy >= 64) continue;
      uint16_t *dst = &e->_fb[dy * 64];
      for (int x2 = 0; x2 < d->iWidthUsed; x2++) {
        int xSrc = d->x + x2;
        if (xSrc < 0 || xSrc >= _jpgW) continue;
        int dx = (int)(((int64_t)xSrc * 64 + _jpgW / 2) / _jpgW);
        if (dx >= 0 && dx < 64) dst[dx] = px[j * stride + x2];
      }
    }
    e->_fbDirty = 1;
    return 1;
  }

  // ---------- GIF ----------
  void freeGifBuf() {
    if (_gifBuf) { free(_gifBuf); _gifBuf = nullptr; }
    _gifBufLen = 0;
  }

  bool openGif(const char *path) {
    _gif.close();
    _gifOpen = false;
    freeGifBuf();
    File f = SPIFFS.open(path, FILE_READ);
    if (!f) return false;
    size_t sz = f.size();
    if (sz == 0 || sz > PIC_LIMIT_BYTES) { f.close(); return false; }
    _gifBuf = (uint8_t *)malloc(sz);
    if (!_gifBuf) { f.close(); Serial.println("[pic] gif oom"); return false; }
    _gifBufLen = sz;
    size_t got = f.read(_gifBuf, sz);
    f.close();
    if (got != sz) { freeGifBuf(); return false; }

    _gif.begin(GIF_PALETTE_RGB565_LE);                   // 按 RGB565 小端解码
    int orc = _gif.open(_gifBuf, (int)sz, gifDrawCb);   // 整段内存解码
    // AnimatedGIF: open 成功返回 1,失败返回 0
    if (orc != 1) {
      freeGifBuf();
      Serial.printf("[pic] gif open fail rc=%d err=%d\n", orc, _gif.getLastError());
      return false;
    }
    _canvasW = _gif.getCanvasWidth();
    _canvasH = _gif.getCanvasHeight();
    if (_canvasW == 0) _canvasW = 1;
    if (_canvasH == 0) _canvasH = 1;
    _gifOpen = true;
    Serial.printf("[pic] gif opened %lux%lu\n", _canvasW, _canvasH);
    return true;
  }

  static void gifDrawCb(GIFDRAW *d) {
    PictureEngine *e = instance;
    if (!e) return;
    uint16_t *pal = d->pPalette;             // RGB565 小端
    if (!pal) return;
    int cw = (int)e->_canvasW, ch = (int)e->_canvasH;
    if (cw <= 0 || ch <= 0) { cw = d->iCanvasWidth > 0 ? d->iCanvasWidth : 64; ch = 64; }
    int rowY = d->iY + d->y;                 // 画布绝对行
    int x0 = d->iX;
    uint8_t *pix = d->pPixels;
    int wFrame = d->iWidth, hFrame = d->iHeight;
    bool opaqueFull = (d->iX == 0 && d->iY == 0 && wFrame >= cw && hFrame >= ch);
    // 整幅不透明帧：先填黑再逐行画，避免上一帧残影
    if (d->y == 0 && opaqueFull) {
      for (int i = 0; i < 64 * 64; i++) e->_fb[i] = 0;
    }
    for (int x = 0; x < wFrame; x++) {
      uint8_t idx = pix[x];
      if (d->ucHasTransparency && idx == d->ucTransparent) continue;
      uint16_t col = pal[idx];
      int dstX = (int)(((int64_t)(x0 + x) * 64 + cw / 2) / cw);
      int dstY = (int)(((int64_t)rowY * 64 + ch / 2) / ch);
      if (dstX >= 0 && dstX < 64 && dstY >= 0 && dstY < 64)
        e->_fb[dstY * 64 + dstX] = col;
    }
    e->_fbDirty = 1;
  }

  void playOneGifFrame() {
    if (!_gifOpen) return;
    int delayMs = 100;
    int r = _gif.playFrame(true, &delayMs, this);
    _nextMs = millis() + (delayMs > 0 ? (unsigned long)delayMs : 80);
    paint();
    if (r == 0) {
      // 播完一轮，重新打开继续循环
      _gif.close();
      _gifOpen = false;
      freeGifBuf();
      if (openGif(PIC_PATH)) {
        clearFb();
        _nextMs = millis() + 100;
      }
    }
  }

  void loop() {
    if (_srv) _srv->handleClient();
    if (_showIp) {
      if ((long)(millis() - _ipUntil) >= 0) { _showIp = false; refresh(); }
      return;   // 显示 IP 期间暂停画面更新
    }
    if (_kind == K_GIF && _gifOpen) {
      if ((long)(millis() - _nextMs) >= 0) playOneGifFrame();
    }
  }

  // ---------- HTTP 上传 ----------
  void startWebServer() {
    if (_srv) return;
    _srv = new WebServer(80);
    _srv->on("/", HTTP_GET, []() {
      PictureEngine *e = instance;
      String page = uploadPage();
      if (e) {
        // 固定入口:连热点 Clockwise-Foto 时始终用 192.168.4.1;另连了 WiFi 就补个局域网 IP
        String ip = "192.168.4.1(连 Clockwise-Foto)";
        if (WiFi.status() == WL_CONNECTED)
          ip += " · 局域网 " + WiFi.localIP().toString();
        page.replace("__IP__", ip);
        page.replace("__BR__", String(e->getBrightness()));
      }
      e->_srv->send(200, "text/html; charset=utf-8", page);
    });
    _srv->on("/brightness", HTTP_GET, []() {
      PictureEngine *e = instance;
      if (!e) return;
      if (e->_srv->hasArg("v")) {
        e->setBrightness(e->_srv->arg("v").toInt());
        e->_srv->send(200, "text/plain; charset=utf-8", "OK");
      } else {
        e->_srv->send(400, "text/plain", "need ?v=0..255");
      }
    });
    _srv->on("/upload", HTTP_POST, []() {
      // 上传完成：先解码显示，再把结果返回给页面
      if (instance) {
        instance->setImageFile(PIC_PATH);
        String msg = instance->_lastStatus.length() ? instance->_lastStatus : "已处理";
        instance->_srv->send(200, "text/plain; charset=utf-8", msg);
      }
    }, []() { uploadHandler(); });
    _srv->on("/clear", HTTP_POST, []() {
      SPIFFS.remove(PIC_PATH);
      SPIFFS.remove(DRAW_PATH);
      if (instance) {
        instance->stopGif();
        instance->_kind = PictureEngine::K_NONE;
        instance->clearFb(); instance->paint();
      }
      if (instance && instance->_srv) instance->_srv->send(200, "text/plain; charset=utf-8", "已清空");
    });
    _srv->on("/paint", HTTP_GET, []() {
      PictureEngine *e = instance;
      if (!e || !e->_srv) return;
      e->_srv->send(200, "text/html; charset=utf-8", paintPage());
    });
    _srv->on("/paint", HTTP_POST, []() {
      PictureEngine *e = instance;
      if (!e || !e->_srv) return;
      String d = e->_srv->hasArg("d") ? e->_srv->arg("d") : String();
      if ((int)d.length() < 64 * 64) {
        e->_srv->send(400, "text/plain; charset=utf-8",
                      "数据不完整(" + String(d.length()) + "/4096)");
        return;
      }
      e->applyDraw(d);
      e->_srv->send(200, "text/plain; charset=utf-8", e->_lastStatus);
    });
    _srv->onNotFound([]() {
      PictureEngine *e = instance;
      if (!e || !e->_srv) return;
      if (e->_srv->method() == HTTP_GET) {   // 手机联网探测/输错路径 → 跳到上传页
        e->_srv->sendHeader("Location", "/");
        e->_srv->send(302, "text/plain", "redirect");
      } else {
        e->_srv->send(404, "text/plain", "Not Found");
      }
    });
    _srv->begin();
    Serial.printf("[pic] http server: AP http://%s/", WiFi.softAPIP().toString().c_str());
    if (WiFi.status() == WL_CONNECTED)
      Serial.printf(" | LAN http://%s/", WiFi.localIP().toString().c_str());
    Serial.println();
  }

  static void uploadHandler() {
    PictureEngine *e = instance;
    if (!e) return;
    HTTPUpload &up = e->_srv->upload();
    if (up.status == UPLOAD_FILE_START) {
      e->stopGif();
      SPIFFS.remove(DRAW_PATH);   // 上传图片会顶掉绘画
      if (e->_upFile) e->_upFile->close();
      e->_upFile = new File(SPIFFS.open(PIC_PATH, FILE_WRITE));
      e->_upSize = 0;
      Serial.printf("[pic] upload start: %s (%u bytes)\n", up.filename.c_str(), up.totalSize);
    } else if (up.status == UPLOAD_FILE_WRITE) {
      if (e->_upFile && *e->_upFile) {
        if (e->_upSize + up.currentSize <= PIC_LIMIT_BYTES) {
          e->_upFile->write(up.buf, up.currentSize);
          e->_upSize += up.currentSize;
        }
      }
    } else if (up.status == UPLOAD_FILE_END) {
      if (e->_upFile) { e->_upFile->close(); delete e->_upFile; e->_upFile = nullptr; }
      Serial.printf("[pic] upload done: %u bytes\n", e->_upSize);
    } else if (up.status == UPLOAD_FILE_ABORTED) {
      if (e->_upFile) { e->_upFile->close(); delete e->_upFile; e->_upFile = nullptr; }
    }
  }

  File *_upFile = nullptr;
  size_t _upSize = 0;

  void stopGif() {
    if (_gifOpen) { _gif.close(); _gifOpen = false; }
    freeGifBuf();
  }

  // ---------- 绘画 ----------
  // 读取 SPIFFS 里保存的 64x64 RGB565 绘画并显示
  void loadDrawFile() {
    File f = SPIFFS.open(DRAW_PATH, FILE_READ);
    if (!f) { _kind = K_NONE; clearFb(); paint(); return; }
    size_t got = f.read((uint8_t *)_fb, sizeof(_fb));
    f.close();
    if (got != sizeof(_fb)) {
      _kind = K_NONE; clearFb(); paint();
      _lastStatus = "绘画文件损坏,请重新画";
      return;
    }
    _kind = K_DRAW;
    _fbDirty = 1; paint();
    Serial.println("[pic] draw loaded");
    _lastStatus = "OK: 上次的绘画";
  }

  // 网页 POST 4096 个十六进制调色板索引(行优先, 0-9A-F) → 画到屏并保存
  void applyDraw(const String &d) {
    for (int i = 0; i < 64 * 64 && i < (int)d.length(); i++) {
      char c = d[i];
      int v = 0;
      if (c >= '0' && c <= '9') v = c - '0';
      else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
      else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
      _fb[i] = DRAW_PAL[v & 15];
    }
    if (_gifOpen) { _gif.close(); _gifOpen = false; }   // 绘画会顶掉播放中的 GIF
    freeGifBuf();
    _kind = K_DRAW;
    File f = SPIFFS.open(DRAW_PATH, "w");
    if (f) { f.write((const uint8_t *)_fb, sizeof(_fb)); f.close(); }
    SPIFFS.remove(PIC_PATH);   // 与图片互斥：画了就删旧图,保证开机显绘画
    _fbDirty = 1; paint();
    Serial.println("[pic] draw applied");
    _lastStatus = "OK: 已画到屏上并保存";
  }

  // /paint 绘画板页面:64x64 网格,颜色/画笔可调,抬手即发送
  static String paintPage() {
    return String(
      "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>绘画板 - Clockwise</title><style>"
      "body{font-family:sans-serif;margin:1em;text-align:center;max-width:480px;margin-left:auto;margin-right:auto}"
      "h1{font-size:1.2em}"
      "#pal{display:flex;flex-wrap:wrap;justify-content:center;gap:8px;margin:10px 0}"
      ".sw{width:36px;height:36px;border-radius:10px;border:2px solid #999;padding:0}"
      ".sw.on{border:3px solid #000;box-shadow:0 0 0 2px #fff;transform:scale(1.1)}"
      "#pc{width:min(92vw,420px);height:auto;touch-action:none;border:1px solid #555;background:#000;border-radius:6px}"
      "button{font-size:1.1em;padding:8px 16px;margin:6px 4px}"
      "#st{min-height:1.4em;margin:4px}"
      "</style></head><body>"
      "<h1>绘画板 (64×64)</h1>"
      "<div style='font-size:.9em;color:#666'>画在手机上，直接上屏。抬手/发送后自动保存</div>"
      "<div id='pal'></div>"
      "<div>画笔粗细: <span id='brush'></span></div>"
      "<canvas id='pc' width='640' height='640'></canvas>"
      "<div id='st'></div>"
      "<button onclick='clearAll()'>清空</button>"
      "<button onclick='sendGrid()' style='font-weight:bold'>发送到屏幕</button>"
      "<p><a href='/'>← 返回上传图片</a></p>"
      "<script>"
      "var PAL=['#000000','#ffffff','#ff3b30','#ff9500','#ffcc00','#34c759','#00c7be','#007aff','#af52de','#ff2d55','#8b5a2b','#c0c0c0','#808080','#a5deff','#ffe4b5','#2c2c2e'];"
      "var G=64,S=10,grid=new Uint8Array(G*G),col=1,br=1,down=false,lx=-1,ly=-1;"
      "var cv=document.getElementById('pc'),ctx=cv.getContext('2d');"
      "function pal(){var d=document.getElementById('pal');for(var i=0;i<16;i++){var b=document.createElement('button');b.className='sw';b.style.background=PAL[i];(function(k){b.onclick=function(){col=k;var c=d.children;for(var j=0;j<16;j++)c[j].className='sw'+(j===k?' on':'');};})(i);d.appendChild(b);}d.children[1].className='sw on';}"
      "function brushes(){var d=document.getElementById('brush');for(var s=1;s<=4;s++){(function(k){var b=document.createElement('button');b.textContent=k;b.onclick=function(){br=k;};d.appendChild(b);})(s);}}"
      "function cellp(x,y){ctx.fillStyle=PAL[grid[y*G+x]];ctx.fillRect(x*S,y*S,S,S);}"
      "function stamp(x,y){var r=(br-1)>>1;for(var j=Math.max(0,y-r);j<=Math.min(G-1,y-r+br-1);j++)for(var i=Math.max(0,x-r);i<=Math.min(G-1,x-r+br-1);i++){grid[j*G+i]=col;cellp(i,j);}}"
      "function seg(x,y){var n=Math.max(Math.abs(x-lx),Math.abs(y-ly));for(var t=0;t<=n;t++)stamp(Math.round(lx+(x-lx)*t/n),Math.round(ly+(y-ly)*t/n));lx=x;ly=y;}"
      "function pxy(e){var r=cv.getBoundingClientRect();return[Math.max(0,Math.min(G-1,Math.floor((e.clientX-r.left)*G/r.width))),Math.max(0,Math.min(G-1,Math.floor((e.clientY-r.top)*G/r.height)))];}"
      "function paintAll(){for(var y=0;y<G;y++)for(var x=0;x<G;x++)cellp(x,y);}"
      "function gridHex(){var h='';for(var i=0;i<grid.length;i++)h+=grid[i].toString(16);return h;}"
      "function sendGrid(){fetch('/paint',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'d='+gridHex()}).then(function(r){return r.text();}).then(function(t){document.getElementById('st').textContent=t;});}"
      "function clearAll(){grid.fill(0);paintAll();sendGrid();}"
      "cv.addEventListener('pointerdown',function(e){e.preventDefault();cv.setPointerCapture(e.pointerId);down=true;var p=pxy(e);lx=p[0];ly=p[1];stamp(p[0],p[1]);});"
      "cv.addEventListener('pointermove',function(e){if(!down)return;e.preventDefault();var p=pxy(e);seg(p[0],p[1]);});"
      "function up(e){if(!down)return;down=false;lx=ly=-1;sendGrid();}"
      "cv.addEventListener('pointerup',up);cv.addEventListener('pointercancel',up);"
      "pal();brushes();paintAll();"
      "</script></body></html>"
    );
  }

  static String uploadPage() {
    return String(
      "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>图片上传 - myclockwise</title><style>body{font-family:sans-serif;text-align:center;margin:2em}input,button{font-size:1.2em}"
      "h1{font-size:1.3em}</style></head><body>"
      "<h1>上传要显示的图片 / GIF</h1>"
      "<p>设备IP: __IP__ &nbsp;·&nbsp; 支持 PNG / JPG / GIF</p>"
      "<p>小贴士: 屏是 64x64，图片自动缩放；JPG/PNG 静态显示，GIF 动画。<b>GIF 需 &le;480x480</b>，文件 &le;500KB。</p>"
      "<div>亮度: <input type='range' id='br' min='0' max='255' value='__BR__' "
      "oninput='document.getElementById(\"brv\").textContent=this.value' "
      "onchange='fetch(\"/brightness?v=\"+this.value)'>&nbsp;<b id='brv'>__BR__</b></div>"
      "<form action='/upload' method='post' enctype='multipart/form-data'>"
      "<input type='file' name='pic' accept='image/png,image/gif,image/*'><br><br>"
      "<button type='submit'>上传并显示</button></form><br>"
      "<p><a href='/paint' style='font-size:1.2em'>✏️ 打开绘画板(直接画到屏幕)</a></p>"
      "<form action='/clear' method='post'><button style='color:#c00'>清空显示(黑屏)</button></form>"
      "</body></html>"
    );
  }
};

const uint16_t PictureEngine::DRAW_PAL[16] = {
  0x0000,  // 0 黑背景/橡皮
  0xFFFF,  // 1 白
  0xF9C6,  // 2 红  #ff3b30
  0xFCA0,  // 3 橙  #ff9500
  0xFE60,  // 4 黄  #ffcc00
  0x362B,  // 5 绿  #34c759
  0x0637,  // 6 青  #00c7be
  0x03DF,  // 7 蓝  #007aff
  0xAA9B,  // 8 紫  #af52de
  0xF96A,  // 9 粉  #ff2d55
  0x8AC5,  // A 棕  #8b5a2b
  0xC618,  // B 浅灰 #c0c0c0
  0x8410,  // C 灰  #808080
  0xA6FF,  // D 浅蓝 #a5deff
  0xFF36,  // E 肤色 #ffe4b5
  0x2965,  // F 深灰 #2c2c2e
};

PictureEngine *PictureEngine::instance = nullptr;
PNG *PictureEngine::_png = nullptr;
uint16_t *PictureEngine::_pngRowBuf = nullptr;
int PictureEngine::_pngW = 0, PictureEngine::_pngH = 0;
int PictureEngine::_jpgW = 0, PictureEngine::_jpgH = 0;
