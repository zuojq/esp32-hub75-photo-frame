# 固件入口：只编译 cw-commons + cw-picture + 本入口文件
idf_component_register(
    SRC_DIRS "."
    REQUIRES cw-commons cw-picture arduino
)

# 固件标识(默认热点/网页标题沿用 Clockwise 命名,可自行修改)
target_compile_options(${COMPONENT_TARGET} PUBLIC -DCW_FW_VERSION="1.0.0" -DCW_FW_NAME="Clockwise")
