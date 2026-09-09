# 图片引擎：解码 PNG/JPG/GIF → 缩放显示 → HTTP 上传 / 绘画板
idf_component_register(
    INCLUDE_DIRS "."
    REQUIRES arduino cw-commons
             ESP32-HUB75-MatrixPanel-I2S-DMA Adafruit-GFX-Library
             PNGdec AnimatedGIF JPEGDEC
)
