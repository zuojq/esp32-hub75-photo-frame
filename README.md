# esp32-hub75-photo-frame

把一块 **64×64 HUB75 RGB LED 点阵屏**变成随时可换内容的 **WiFi 相框 / 动图画板 / 像素画板**：手机连上它自带的热点就能上传图片或 GIF，还能打开网页**直接画画**，画完立刻出现在屏上。

Turn a **64×64 HUB75 LED matrix** into a **WiFi photo frame / GIF player / pixel drawing board**: connect your phone to its own hotspot and upload a picture or GIF, or open the built-in **paint page** and draw right onto the screen.

> 本项目是固件源码的精简开源版，只包含作者自己写的代码；不含第三方 ESP-IDF 组件。也提供**预编译固件**（`bin/`），不想折腾编译可直接烧录。
> This is a lean, source-only release containing just the author's own code (no third‑party components). A **prebuilt firmware** is in `bin/` — flash it if you don't want to build.

---

## 简体中文

### 功能

- 📱 **永久热点，固定地址**：开机即开热点 `Clockwise-Foto`（密码同名，均 `Clockwise-Foto`），入口永远是 **`http://192.168.4.1/`**，不会迷路
- 📶 **自动弹页(captive portal)**：拦截 DNS，手机一连上热点就被系统识别为“需登录的 WiFi”，自动弹出上传页
- 🖼️ **手机传图**：支持 **PNG / JPG / GIF**，自动缩放铺满 64×64
- 🎞️ **GIF 动画**：循环播放（限 ≤480×480）
- ✏️ **绘画板**：网页 `/paint`，16 色调色板 + 画笔粗细 1–4，抬手即上屏，自动保存
- 💡 **亮度调节**：网页滑杆，0–255，断电记忆
- 🔑 **按键**（GPIO36）：短按在屏上显示地址
- 🔁 **双模共存**：若曾配过 WiFi 会同时自动连上，局域网 IP 也能访问（可选项）
- 💾 **开机恢复**：上次的图片 / GIF / 绘画断电后还在

### 硬件

- ESP32（经典版 WROOM 即可）
- 64×64 HUB75 P3/P4 点阵屏（本套件为 **RBG** 面板，即绿/蓝线相反；固件已固定开启绿蓝互换）
- **独立 5V 电源**给屏供电（大电流！USB 供电不足会出现颜色错乱/抖动，不是代码问题）
- （可选）按键接 GPIO36 与 GND，短按看地址

接线引脚（HUB75 标准默认映射，具体以你买的屏/排线为准）：

| R1 | G1 | B1 | R2 | G2 | B2 | A | B | C | D | E | LAT | OE | CLK |
|----|----|----|----|----|----|---|---|---|---|---|-----|----|----|
| 25 | 26 | 27 | 14 | 12 | 13 | 23 | 19 | 5 | 17 | 18 | 4 | 15 | 16 |

### 快速上手（免编译）

1. 烧录 `bin/esp32-hub75-photo-frame.bin`（整片镜像，0x0 起烧）
   ```bash
   python -m esptool --chip esp32 --port COM3 write-flash 0x0 bin/esp32-hub75-photo-frame.bin
   ```
2. 手机连 WiFi **`Clockwise-Foto`**，密码 **`Clockwise-Foto`**
3. 浏览器打开 **`192.168.4.1`** → 上传图片/GIF，或点 **✏️ 打开绘画板** 直接画

> 屏是 64×64：图片自动缩放；GIF 动画、单帧图片需 ≤480×480、建议 ≤500KB。

### 从源码构建

> ⚠️ 精简仓库不含第三方组件。要本机编译，除本仓库代码外还需把下列 **ESP-IDF 组件**放进 `components/`（开发环境为 **ESP-IDF v6.x + Arduino as IDF component**；下列为开发验证过的来源/版本与必要改动）：

| 组件 | 来源 / 版本 | 备注 |
|------|------------|------|
| arduino | espressif/arduino-esp32（支持 IDF 6.1 的近期提交） | 编译需 `-Wno-missing-field-initializers` |
| ESP32-HUB75-MatrixPanel-I2S-DMA | mrfaptastic/…（适配 IDF 6 的分支） | I2S1/periph 相关需改到新 API |
| AnimatedGIF | tag `2.2.0` | 配自写 `CMakeLists.txt`；整段读入内存后 `open()`；master 有崩溃 bug 勿用 |
| PNGdec / JPEGDEC | bitbank2/PNGdec、bitbank2/JPEGDEC | 配自写 `CMakeLists.txt` |
| Adafruit-GFX-Library | adafruit/… | 与 HUB75 库配套 |

> 版权归各自作者；本仓库不打包它们。**如果不想手动拼组件，最省事的是用 `bin/` 里的预编译固件。**完整可编译工程（含全部组件与 IDF6 适配补丁）作者本地有，可另提供。

构建：
```bash
idf.py set-target esp32
idf.py build
idf.py -p COM3 flash
```

### 目录结构

```
firmware/
  src/main.cpp                     # 入口：开热点→连已存WiFi→启动网页
  lib/cw-picture/PictureEngine.h   # 图片引擎：解码/缩放/上传页/绘画板(核心)
  lib/cw-commons/                  # CWPreferences(NVS配置) + DisplayController
main/main.cpp                      # 薄封装，包含 firmware/src/main.cpp
huge_app.csv                       # 4MB 分区表(3MB app + SPIFFS 存图)
sdkconfig.defaults
bin/                               # 预编译整片固件(可直接烧)
```

### 常用自定义

- 改热点名/密码：`firmware/src/main.cpp` 顶部 `AP_SSID` / `AP_PASSWORD`
- 换 RGB 面板接线：`firmware/lib/cw-commons/DisplayController.h` 里 `swapBlueGreen`
- 调绘画 16 色调色板：`PictureEngine.h` 的 `DRAW_PAL[]` 与 `/paint` 页面里 `PAL[]`（两者顺序一一对应）
- 改默认亮度等：网页亮度滑杆会自动存 NVS

### 许可与致谢

MIT。本固件二次开发自 [jnthas/clockwise](https://github.com/jnthas/clockwise)（MIT），保留原作者版权声明。依赖的第三方库各自保留其许可。

---

## English

### Features

- 📱 **Always-on hotspot, fixed address** — boots a `Clockwise-Foto` AP (password = name, i.e. `Clockwise-Foto`); entry point is **always `http://192.168.4.1/`**
- 📶 **Captive portal** — DNS is hijacked so phones auto-detect a “sign-in required” network and pop up the page automatically
- 🖼️ **Upload PNG / JPG / GIF** — auto-scaled to fill 64×64
- 🎞️ **Animated GIF** — loops (≤480×480)
- ✏️ **Paint board** at `/paint` — 16-color palette, brush size 1–4, sends to the panel on finger-up, auto-saved
- 💡 **Brightness slider** 0–255, persisted in NVS
- 🔑 **Button** on GPIO36 shows the address briefly
- 🔁 **Dual mode** — also auto-joins a previously configured WiFi for LAN access (optional)
- 💾 **Persistence** — last image/GIF/drawing survives power-off

### Hardware

- ESP32 (classic WROOM)
- 64×64 HUB75 P3/P4 matrix (this kit is an **RBG** panel — green/blue swapped; firmware enables swap)
- **Separate 5 V supply** for the panel (high current! powering from USB may cause wrong colours / flicker)
- Optional button: GPIO36 → GND (short press shows the address)

Pin map (standard HUB75 mapping, adapt to your panel):

| R1 | G1 | B1 | R2 | G2 | B2 | A | B | C | D | E | LAT | OE | CLK |
|----|----|----|----|----|----|---|---|---|---|---|-----|----|----|
| 25 | 26 | 27 | 14 | 12 | 13 | 23 | 19 | 5 | 17 | 18 | 4 | 15 | 16 |

### Quick start (no build)

1. Flash `bin/esp32-hub75-photo-frame.bin` at offset `0x0` (full image):
   ```bash
   python -m esptool --chip esp32 --port COM3 write-flash 0x0 bin/esp32-hub75-photo-frame.bin
   ```
2. Join WiFi **`Clockwise-Foto`**, password **`Clockwise-Foto`**
3. Open **`192.168.4.1`** → upload an image/GIF, or open **✏️ 打开绘画板** and draw.

> The panel is 64×64: images are auto-scaled; GIF ≤480×480 and files ≤~500 KB are recommended.

### Build from source

> ⚠️ This lean repo ships only the author's own code — no third‑party IDF components. To build you must also place the components below into `components/` (developed against **ESP-IDF v6.x with Arduino as an IDF component**):

| Component | Source / version | Notes |
|-----------|------------------|-------|
| arduino | espressif/arduino-esp32 (recent commit supporting IDF 6.1) | add `-Wno-missing-field-initializers` |
| ESP32-HUB75-MatrixPanel-I2S-DMA | mrfaptastic/… (IDF6-adapted branch) | I2S1/periph moved to new APIs |
| AnimatedGIF | tag `2.2.0` | ship own `CMakeLists.txt`; read whole file into RAM then `open()`; avoid master (crash bug) |
| PNGdec / JPEGDEC | bitbank2/PNGdec, bitbank2/JPEGDEC | ship own `CMakeLists.txt` |
| Adafruit-GFX-Library | adafruit/… | used alongside the HUB75 lib |

> All belong to their respective authors and are not bundled here. **Use the prebuilt `bin/` firmware if you don't want to assemble components.** A fully buildable project (with all components + IDF6 patches) is available from the author on request.

```bash
idf.py set-target esp32
idf.py build
idf.py -p COM3 flash
```

### Layout

```
firmware/
  src/main.cpp                     # entry: hotspot → optional saved-WiFi → web server
  lib/cw-picture/PictureEngine.h   # image engine: decode/scale/upload page/paint board (core)
  lib/cw-commons/                  # CWPreferences (NVS) + DisplayController
main/main.cpp                      # thin wrapper including firmware/src/main.cpp
huge_app.csv                       # 4 MB partition table (3 MB app + SPIFFS)
sdkconfig.defaults
bin/                               # prebuilt full flash image
```

### Common tweaks

- Hotspot name/password: `AP_SSID` / `AP_PASSWORD` at the top of `firmware/src/main.cpp`
- RGB/RBG wiring: `swapBlueGreen` in `firmware/lib/cw-commons/DisplayController.h`
- 16-color palette: `DRAW_PAL[]` in `PictureEngine.h` matches `PAL[]` in the `/paint` page (same order)
- Default brightness etc.: set once from the web page (persisted in NVS)

### License & credits

MIT. Firmware is a rework of [jnthas/clockwise](https://github.com/jnthas/clockwise) (MIT) with the original copyright retained. Third-party dependencies keep their own licenses.
