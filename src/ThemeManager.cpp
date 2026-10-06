#include "ThemeManager.h"
#include "implot.h"

namespace {

struct Palette {
    ImVec4 bg, surface, surfaceHi, frame, border, text, muted, accent, accentHi;
    ImVec4 success, warning, danger, transfer;
    bool light;
};

ImVec4 Hex(unsigned rgb, float a = 1.0f) {
    return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a);
}

ImVec4 WithAlpha(ImVec4 c, float a) {
    c.w = a;
    return c;
}

ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

Palette GetPalette(AppTheme theme) {
    switch (theme) {
        case AppTheme::TokyoNight:
            return {Hex(0x1A1B26), Hex(0x1F2335), Hex(0x292E42), Hex(0x24283B), Hex(0x3B4261), Hex(0xC0CAF5),
                    Hex(0x8089B3), Hex(0x7AA2F7), Hex(0x9ABDF5), Hex(0x9ECE6A), Hex(0xE0AF68), Hex(0xF7768E),
                    Hex(0xBB9AF7), false};
        case AppTheme::Catppuccin:
            return {Hex(0x1E1E2E), Hex(0x24273A), Hex(0x363A4F), Hex(0x313244), Hex(0x45475A), Hex(0xCDD6F4),
                    Hex(0xA6ADC8), Hex(0xCBA6F7), Hex(0xDDC3FA), Hex(0xA6E3A1), Hex(0xF9E2AF), Hex(0xF38BA8),
                    Hex(0x89B4FA), false};
        case AppTheme::NordSlate:
            return {Hex(0x2E3440), Hex(0x343B49), Hex(0x434C5E), Hex(0x3B4252), Hex(0x4C566A), Hex(0xECEFF4),
                    Hex(0xA3ABBC), Hex(0x88C0D0), Hex(0x8FBCBB), Hex(0xA3BE8C), Hex(0xEBCB8B), Hex(0xBF616A),
                    Hex(0xB48EAD), false};
        case AppTheme::OledBlack:
            return {Hex(0x000000), Hex(0x0A0A0A), Hex(0x1C1C1C), Hex(0x141414), Hex(0x2A2A2A), Hex(0xE6E6E6),
                    Hex(0x8C8C8C), Hex(0x3D8BFD), Hex(0x6AA6FF), Hex(0x4ADE80), Hex(0xFACC15), Hex(0xF87171),
                    Hex(0xC084FC), false};
        case AppTheme::LightClean:
            return {Hex(0xF6F7F9), Hex(0xFFFFFF), Hex(0xE8EBF0), Hex(0xE9ECF1), Hex(0xCDD2DA), Hex(0x1F2328),
                    Hex(0x5F6773), Hex(0x2563EB), Hex(0x3B7BF5), Hex(0x15803D), Hex(0xB45309), Hex(0xB91C1C),
                    Hex(0x7C3AED), true};
        case AppTheme::DarkModern:
        default:
            return {Hex(0x16171A), Hex(0x1D1F23), Hex(0x2A2D33), Hex(0x25282E), Hex(0x3A3E46), Hex(0xE8EAED),
                    Hex(0x9AA0A9), Hex(0x4C9AFF), Hex(0x72B0FF), Hex(0x4CC38A), Hex(0xE8B04B), Hex(0xF0616D),
                    Hex(0xA78BFA), false};
    }
}

AppTheme g_theme = AppTheme::DarkModern;
float g_dpi = 1.0f;
ThemeTokens g_tokens;

void ApplySizes(float scale) {
    ImGuiStyle& s = ImGui::GetStyle();
    // Start from default sizes so repeated theme/DPI changes never compound
    // ScaleAllSizes; colours and font sizing are preserved.
    ImGuiStyle fresh;
    for (int i = 0; i < ImGuiCol_COUNT; i++) fresh.Colors[i] = s.Colors[i];
    fresh.FontSizeBase = s.FontSizeBase;
    fresh.FontScaleMain = s.FontScaleMain;
    s = fresh;
    s.FramePadding     = ImVec2(8, 5);
    s.ItemSpacing      = ImVec2(8, 6);
    s.ItemInnerSpacing = ImVec2(6, 4);
    s.CellPadding      = ImVec2(6, 4);
    s.WindowPadding    = ImVec2(12, 12);
    s.ScrollbarSize    = 14.0f;
    s.GrabMinSize      = 12.0f;
    s.WindowRounding   = 6.0f;
    s.ChildRounding    = 6.0f;
    s.FrameRounding    = 4.0f;
    s.PopupRounding    = 6.0f;
    s.TabRounding      = 4.0f;
    s.GrabRounding     = 4.0f;
    s.ScrollbarRounding = 6.0f;
    s.WindowBorderSize = 1.0f;
    s.ChildBorderSize  = 1.0f;
    s.FrameBorderSize  = 0.0f;
    s.ScaleAllSizes(scale);
    s.FontScaleDpi = scale;
}

} // namespace

void ThemeManager::ApplyTheme(AppTheme theme) {
    g_theme = theme;
    const Palette p = GetPalette(theme);

    if (p.light) ImGui::StyleColorsLight();
    else ImGui::StyleColorsDark();
    ApplySizes(g_dpi);

    ImVec4* c = ImGui::GetStyle().Colors;
    c[ImGuiCol_Text]                 = p.text;
    c[ImGuiCol_TextDisabled]         = p.muted;
    c[ImGuiCol_WindowBg]             = p.bg;
    c[ImGuiCol_ChildBg]              = WithAlpha(p.surface, 0.0f);
    c[ImGuiCol_PopupBg]              = p.surface;
    c[ImGuiCol_Border]               = p.border;
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]              = p.frame;
    c[ImGuiCol_FrameBgHovered]       = p.surfaceHi;
    c[ImGuiCol_FrameBgActive]        = Mix(p.surfaceHi, p.accent, 0.25f);
    c[ImGuiCol_TitleBg]              = p.surface;
    c[ImGuiCol_TitleBgActive]        = p.surfaceHi;
    c[ImGuiCol_TitleBgCollapsed]     = p.surface;
    c[ImGuiCol_MenuBarBg]            = p.surface;
    c[ImGuiCol_ScrollbarBg]          = WithAlpha(p.bg, 0.6f);
    c[ImGuiCol_ScrollbarGrab]        = p.border;
    c[ImGuiCol_ScrollbarGrabHovered] = Mix(p.border, p.text, 0.2f);
    c[ImGuiCol_ScrollbarGrabActive]  = p.accent;
    c[ImGuiCol_CheckMark]            = p.accent;
    c[ImGuiCol_SliderGrab]           = p.accent;
    c[ImGuiCol_SliderGrabActive]     = p.accentHi;
    c[ImGuiCol_Button]               = WithAlpha(p.accent, p.light ? 0.16f : 0.22f);
    c[ImGuiCol_ButtonHovered]        = WithAlpha(p.accent, p.light ? 0.30f : 0.40f);
    c[ImGuiCol_ButtonActive]         = WithAlpha(p.accent, p.light ? 0.45f : 0.60f);
    c[ImGuiCol_Header]               = WithAlpha(p.accent, 0.20f);
    c[ImGuiCol_HeaderHovered]        = WithAlpha(p.accent, 0.32f);
    c[ImGuiCol_HeaderActive]         = WithAlpha(p.accent, 0.45f);
    c[ImGuiCol_Separator]            = p.border;
    c[ImGuiCol_SeparatorHovered]     = p.accent;
    c[ImGuiCol_SeparatorActive]      = p.accentHi;
    c[ImGuiCol_ResizeGrip]           = WithAlpha(p.accent, 0.20f);
    c[ImGuiCol_ResizeGripHovered]    = WithAlpha(p.accent, 0.60f);
    c[ImGuiCol_ResizeGripActive]     = p.accent;
    c[ImGuiCol_Tab]                  = p.surface;
    c[ImGuiCol_TabHovered]           = WithAlpha(p.accent, 0.35f);
    c[ImGuiCol_TabSelected]          = Mix(p.surface, p.accent, 0.25f);
    c[ImGuiCol_TabSelectedOverline]  = p.accent;
    c[ImGuiCol_TabDimmed]            = p.surface;
    c[ImGuiCol_TabDimmedSelected]    = p.surfaceHi;
    c[ImGuiCol_PlotLines]            = p.accent;
    c[ImGuiCol_PlotHistogram]        = p.accent;
    c[ImGuiCol_TableHeaderBg]        = p.surfaceHi;
    c[ImGuiCol_TableBorderStrong]    = p.border;
    c[ImGuiCol_TableBorderLight]     = WithAlpha(p.border, 0.6f);
    c[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]        = WithAlpha(p.light ? Hex(0x000000) : Hex(0xFFFFFF), 0.03f);
    c[ImGuiCol_TextSelectedBg]       = WithAlpha(p.accent, 0.35f);
    c[ImGuiCol_NavCursor]            = p.accent;
    c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0, 0, 0, p.light ? 0.25f : 0.55f);

    // ImPlot keeps its own style (G11).
    if (p.light) ImPlot::StyleColorsLight();
    else ImPlot::StyleColorsDark();
    ImPlotStyle& ps = ImPlot::GetStyle();
    ps.Colors[ImPlotCol_FrameBg]    = WithAlpha(p.bg, 0.0f);
    ps.Colors[ImPlotCol_PlotBg]     = p.surface;
    ps.Colors[ImPlotCol_PlotBorder] = p.border;
    ps.Colors[ImPlotCol_LegendBg]   = WithAlpha(p.surface, 0.92f);
    ps.Colors[ImPlotCol_AxisText]   = p.muted;
    ps.Colors[ImPlotCol_AxisGrid]   = WithAlpha(p.border, 0.6f);
    ps.Colors[ImPlotCol_InlayText]  = p.text;
    ps.Colors[ImPlotCol_TitleText]  = p.text;

    g_tokens.text              = p.text;
    g_tokens.muted             = p.muted;
    g_tokens.accent            = p.accent;
    g_tokens.success           = p.success;
    g_tokens.warning           = p.warning;
    g_tokens.danger            = p.danger;
    g_tokens.income            = p.success;
    g_tokens.expense           = p.danger;
    g_tokens.transfer          = p.transfer;
    g_tokens.savings           = p.light ? Hex(0x0F766E) : Hex(0x2DD4BF);   // teal: distinct from income green
    g_tokens.cardBg            = p.surface;
    g_tokens.dangerButton      = WithAlpha(p.danger, p.light ? 0.85f : 0.70f);
    g_tokens.dangerButtonHover = p.danger;
}

AppTheme ThemeManager::GetCurrentTheme() { return g_theme; }
const ThemeTokens& ThemeManager::Tokens() { return g_tokens; }

void ThemeManager::SetDpiScale(float scale) {
    if (scale < 0.5f) scale = 0.5f;
    if (scale > 4.0f) scale = 4.0f;
    g_dpi = scale;
    ApplyTheme(g_theme);
}

float ThemeManager::GetDpiScale() { return g_dpi; }

const char* ThemeManager::ThemeName(AppTheme theme) {
    switch (theme) {
        case AppTheme::DarkModern: return "DarkModern";
        case AppTheme::TokyoNight: return "TokyoNight";
        case AppTheme::Catppuccin: return "Catppuccin";
        case AppTheme::NordSlate:  return "NordSlate";
        case AppTheme::OledBlack:  return "OledBlack";
        case AppTheme::LightClean: return "LightClean";
    }
    return "DarkModern";
}

const char* ThemeManager::ThemeLabel(AppTheme theme) {
    switch (theme) {
        case AppTheme::DarkModern: return "Dark Modern";
        case AppTheme::TokyoNight: return "Tokyo Night";
        case AppTheme::Catppuccin: return "Catppuccin";
        case AppTheme::NordSlate:  return "Nord Slate";
        case AppTheme::OledBlack:  return "OLED Black";
        case AppTheme::LightClean: return "Light Clean";
    }
    return "Dark Modern";
}

AppTheme ThemeManager::ThemeFromName(const std::string& name) {
    for (int i = 0; i < kThemeCount; i++) {
        AppTheme t = static_cast<AppTheme>(i);
        if (name == ThemeName(t)) return t;
    }
    return AppTheme::DarkModern;
}
