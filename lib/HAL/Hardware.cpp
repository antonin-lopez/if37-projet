#include "Hardware.h"
#include <M5Unified.h>
#include <cmath>

namespace Hardware
{
    namespace
    {
        constexpr float SCREEN_MARGIN_RATIO = 0.06f;
        constexpr float CENTER_TEXT_SIZE = 3.5f;
        constexpr float SPLIT_TEXT_SIZE = 2.5f;
        constexpr uint8_t HEADER_TEXT_SIZE = 2;
        constexpr float BRIGHTNESS_MIDPOINT = 128.0f;

        constexpr uint32_t COLOR_BLACK = 0x000000;
        constexpr uint32_t COLOR_WHITE = 0xFFFFFF;

        uint32_t currentBgColor_ = COLOR_BLACK;

        // Returns black or white, whichever contrasts better against `color`,
        // using the standard perceptual-luminance weighting for RGB channels.
        uint32_t contrastingTextColor(uint32_t color)
        {
            uint8_t r = (color >> 16) & 0xFF;
            uint8_t g = (color >> 8) & 0xFF;
            uint8_t b = color & 0xFF;
            float brightness = (r * 0.299f) + (g * 0.587f) + (b * 0.114f);
            return brightness > BRIGHTNESS_MIDPOINT ? COLOR_BLACK : COLOR_WHITE;
        }

        // Draws the background, header line and battery readout shared by
        // every screen. Both `display()` overloads call this first.
        void prepareScreen(const char *header, int leftBat, int rightBat)
        {
            M5.Lcd.fillScreen(currentBgColor_);
            M5.Lcd.setTextColor(contrastingTextColor(currentBgColor_));

            int w = M5.Lcd.width();
            int h = M5.Lcd.height();
            int margin = static_cast<int>(w * SCREEN_MARGIN_RATIO);

            int batLevel = M5.Power.getBatteryLevel();
            char batStr[8];
            snprintf(batStr, sizeof(batStr), "%d%%", batLevel);

            M5.Lcd.setTextSize(HEADER_TEXT_SIZE);
            int batW = M5.Lcd.textWidth(batStr);

            M5.Lcd.setCursor(w - batW - margin, margin);
            M5.Lcd.printf("%s", batStr);

            M5.Lcd.setCursor(margin, margin);
            M5.Lcd.printf("%s", header);

            // Footer only makes sense once we know at least one ankle's state.
            if (leftBat != -2 || rightBat != -2)
            {
                M5.Lcd.setTextSize(HEADER_TEXT_SIZE);
                int footerY = h - M5.Lcd.fontHeight() - margin;

                M5.Lcd.setCursor(margin, footerY);
                if (leftBat >= 0)
                    M5.Lcd.printf("L: %d%%", leftBat);
                else
                    M5.Lcd.printf("L: --");

                char rightStr[16];
                if (rightBat >= 0)
                    snprintf(rightStr, sizeof(rightStr), "R: %d%%", rightBat);
                else
                    snprintf(rightStr, sizeof(rightStr), "R: --");

                int rightW = M5.Lcd.textWidth(rightStr);
                M5.Lcd.setCursor(w - rightW - margin, footerY);
                M5.Lcd.printf("%s", rightStr);
            }
        }
    } // namespace

    void init()
    {
        auto cfg = M5.config();
        M5.begin(cfg);
        M5.Lcd.setRotation(3);
    }

    void update() { M5.update(); }
    bool isShortPress() { return M5.BtnA.wasClicked(); }
    bool isLongPress() { return M5.BtnA.wasHold(); }

    float getAccelMagnitude()
    {
        float x, y, z;
        M5.Imu.getAccelData(&x, &y, &z);
        return std::sqrt(x * x + y * y + z * z);
    }

    void setBackgroundColor(uint32_t rgbColor) { currentBgColor_ = rgbColor; }
    void beep(uint32_t frequencyHz, uint32_t durationMs) { M5.Speaker.tone(frequencyHz, durationMs); }

    void display(const char *header, const char *bodyCenter, int leftBat, int rightBat)
    {
        prepareScreen(header, leftBat, rightBat);

        if (bodyCenter && bodyCenter[0] != '\0')
        {
            M5.Lcd.setTextSize(CENTER_TEXT_SIZE);
            int dataW = M5.Lcd.textWidth(bodyCenter);
            int dataH = M5.Lcd.fontHeight();
            M5.Lcd.setCursor((M5.Lcd.width() - dataW) / 2, (M5.Lcd.height() - dataH) / 2);
            M5.Lcd.printf("%s", bodyCenter);
        }
    }

    void display(const char *header, const char *bodyLeft, const char *bodyRight, int leftBat, int rightBat)
    {
        prepareScreen(header, leftBat, rightBat);

        M5.Lcd.setTextSize(SPLIT_TEXT_SIZE);
        int bodyH = M5.Lcd.fontHeight();
        int posYBody = (M5.Lcd.height() - bodyH) / 2;
        int margin = static_cast<int>(M5.Lcd.width() * SCREEN_MARGIN_RATIO);

        if (bodyLeft && bodyLeft[0] != '\0')
        {
            M5.Lcd.setCursor(margin, posYBody);
            M5.Lcd.printf("%s", bodyLeft);
        }

        if (bodyRight && bodyRight[0] != '\0')
        {
            int rightW = M5.Lcd.textWidth(bodyRight);
            M5.Lcd.setCursor(M5.Lcd.width() - rightW - margin, posYBody);
            M5.Lcd.printf("%s", bodyRight);
        }
    }
}
