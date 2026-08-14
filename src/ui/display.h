// Slim Cardputer display: farm scene + bars. No OnePork menu tree.
#pragma once

#include <M5Unified.h>

#define DISPLAY_W 240
#define DISPLAY_H 135
#define TOP_BAR_H 16
#define BOTTOM_BAR_H 14
#define MAIN_H (DISPLAY_H - TOP_BAR_H - BOTTOM_BAR_H)

struct UiStyle {
    static constexpr uint16_t BG     = 0x2145;
    static constexpr uint16_t PANEL  = 0x3A8A;
    static constexpr uint16_t TITLE  = 0xFFE0;
    static constexpr uint16_t TEXT   = 0xEF5D;
    static constexpr uint16_t DIM    = 0x9CD3;
    static constexpr uint16_t PINK   = 0xFDB6;
    static constexpr uint16_t GREEN  = 0x07E0;
    static constexpr uint16_t RED    = 0xF800;
    static constexpr uint16_t GOLD   = 0xFE60;
    static constexpr uint16_t CYAN   = 0x07FF;
    static constexpr uint16_t DIRT   = 0x6A20;
};

enum class NoticeKind : uint8_t {
    REWARD,
    STATUS,
    WARNING,
    ERROR
};

enum class NoticeChannel : uint8_t {
    AUTO,
    TOAST,
    TOP_BAR
};

uint16_t getColorFG();
uint16_t getColorBG();

#define COLOR_BG getColorBG()
#define COLOR_FG getColorFG()
#define COLOR_ACCENT  (UiStyle::PINK)
#define COLOR_WARNING (UiStyle::GOLD)
#define COLOR_DANGER  (UiStyle::RED)
#define COLOR_SUCCESS (UiStyle::GREEN)

void uiListBackground(M5Canvas& canvas);
void uiListRow(M5Canvas& canvas, int y, int lineH, bool selected, uint16_t accent = UiStyle::PINK);

class Display {
public:
    static void init();
    static void update();
    static void pushAll();
    static void showBootSplash();

    static M5Canvas& getTopBar() { return topBar; }
    static M5Canvas& getMain() { return mainCanvas; }
    static M5Canvas& getBottomBar() { return bottomBar; }

    static void showToast(const char* message, uint32_t durationMs = 2000);
    static void notify(NoticeKind kind, const char* message,
                       uint32_t durationMs = 0,
                       NoticeChannel channel = NoticeChannel::AUTO);
    static void setTopBarMessage(const char* message, uint32_t durationMs = 0);
    static void clearTopBarMessage();

    static void triggerScreenShake(uint8_t intensity = 3, uint16_t durationMs = 200);
    static bool isShaking();
    static float getShakeDecay();
    static uint8_t getShakeIntensity();

    static void resetDimTimer();
    static void updateDimming();
    static bool isDimmed() { return dimmed; }
    static void toggleScreenPower();
    static bool isScreenForcedOff() { return screenForcedOff; }

    static uint8_t* mainCanvasBuffer();
    static size_t   mainCanvasBufferSize();

    static void setBottomHint(const char* message);
    static void setBottomOverlay(const char* message);
    static void clearBottomOverlay();
    static bool showConfirmBox(const char* title, const char* message);

private:
    static M5Canvas topBar;
    static M5Canvas mainCanvas;
    static M5Canvas bottomBar;

    static bool screenShakeActive;
    static uint32_t screenShakeStart;
    static uint16_t screenShakeDuration;
    static uint8_t screenShakeIntensity;

    static uint32_t lastActivityTime;
    static bool dimmed;
    static bool screenForcedOff;

    static char toastMessage[160];
    static uint32_t toastStartTime;
    static uint32_t toastDurationMs;
    static bool toastActive;

    static char topBarMessage[96];
    static uint32_t topBarMessageStart;
    static uint32_t topBarMessageDuration;

    static char bottomHint[96];

    static void drawTopBar();
    static void drawBottomBar();
    static void drawFarm();
    static void drawToast();
};
