cmake_minimum_required(VERSION 3.5)

# 自研固件组件（图片引擎 cw-picture + 通用库 cw-commons）
set(EXTRA_COMPONENT_DIRS "firmware/lib")

# 需要外部 ESP-IDF 组件(arduino、HUB75 驱动、图片解码库等),
# 详见 README「从源码构建」。把它们放进 components/ 后:
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32-hub75-photo-frame)
