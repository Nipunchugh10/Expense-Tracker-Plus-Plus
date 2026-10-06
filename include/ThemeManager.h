#pragma once
#include <string>
#include "imgui.h"

enum class AppTheme {
    DarkModern,    // default
    TokyoNight,    // deep navy with neon accents
    Catppuccin,    // warm pastel dark palette
    NordSlate,     // arctic slate blue
    OledBlack,     // true black for OLED panels
    LightClean     // high-readability daylight theme
};

// Semantic colours consumed by every tab instead of hardcoded values (G11).
struct ThemeTokens {
    ImVec4 text;
    ImVec4 muted;
    ImVec4 accent;
    ImVec4 success;
    ImVec4 warning;
    ImVec4 danger;
    ImVec4 income;
    ImVec4 expense;
    ImVec4 transfer;
    ImVec4 savings;
    ImVec4 cardBg;
    ImVec4 dangerButton;
    ImVec4 dangerButtonHover;
};

class ThemeManager {
public:
    static constexpr int kThemeCount = 6;

    static void ApplyTheme(AppTheme theme);          // ImGui + ImPlot colours and sizes
    static AppTheme GetCurrentTheme();
    static const ThemeTokens& Tokens();

    static void SetDpiScale(float scale);            // re-applies sizes for the monitor DPI
    static float GetDpiScale();

    static const char* ThemeName(AppTheme theme);    // stable id stored in settings.json
    static const char* ThemeLabel(AppTheme theme);   // menu label
    static AppTheme ThemeFromName(const std::string& name);
};
