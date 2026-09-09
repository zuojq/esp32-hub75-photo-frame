#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <CWPreferences.h>

struct DisplayController {
    MatrixPanel_I2S_DMA *dma_display = nullptr;

    static DisplayController *getInstance() {
        static DisplayController base;
        return &base;
    }

    void begin() {
        static HUB75_I2S_CFG mxconfig(64, 64, 1);
        if (ClockwiseParams::getInstance()->swapBlueGreen) {
            // Swap Blue and Green pins because the panel is RBG instead of RGB.
            mxconfig.gpio.b1 = 26;
            mxconfig.gpio.b2 = 12;
            mxconfig.gpio.g1 = 27;
            mxconfig.gpio.g2 = 13;
        }

        mxconfig.gpio.e = 18;
        mxconfig.clkphase = false;

        // Display Setup
        dma_display = new MatrixPanel_I2S_DMA(mxconfig);
        dma_display->begin();
        dma_display->setBrightness8(ClockwiseParams::getInstance()->displayBright);
        dma_display->clearScreen();
        dma_display->setRotation(ClockwiseParams::getInstance()->displayRotation);
    }

    MatrixPanel_I2S_DMA *getDmaDisplay() {
        return dma_display;
    }
};