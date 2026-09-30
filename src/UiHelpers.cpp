#include "UiHelpers.h"
#include "Money.h"
#include "ThemeManager.h"
#include "Utils.h"
#include "Validation.h"
#include "misc/cpp/imgui_stdlib.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <unordered_map>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#endif

namespace Ui {

const char* const kMonthNames[12] = {"January", "February", "March", "April", "May", "June",
                                     "July", "August", "September", "October", "November", "December"};
const char* const kMonthShort[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static std::string VisibleLabel(const char* label) {
    std::string s = label;
    size_t pos = s.find("##");
    return pos == std::string::npos ? s : s.substr(0, pos);
}

bool YearInput(const char* label, int& year, float width) {
    ImGui::SetNextItemWidth(width);
    bool changed = ImGui::InputInt(label, &year);
    int clamped = Validation::ClampYear(year);
    if (clamped != year) {
        year = clamped;
        changed = true;
    }
    return changed;
}

bool MonthCombo(const char* label, int& month, bool allowAll, float width) {
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    const char* preview = (month >= 1 && month <= 12) ? kMonthNames[month - 1] : "All months";
    if (ImGui::BeginCombo(label, preview)) {
        if (allowAll && ImGui::Selectable("All months", month == 0)) { month = 0; changed = true; }
        for (int m = 1; m <= 12; m++) {
            if (ImGui::Selectable(kMonthNames[m - 1], month == m)) { month = m; changed = true; }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool DateInput(const char* id, Date& date) {
    ImGui::PushID(id);
    const float unit = ImGui::GetFontSize();
    bool changed = false;
    ImGui::SetNextItemWidth(unit * 6.5f);
    changed |= ImGui::InputInt("##y", &date.year);
    ImGui::SameLine(0, 4);
    ImGui::SetNextItemWidth(unit * 5.5f);
    changed |= ImGui::InputInt("##m", &date.month);
    ImGui::SameLine(0, 4);
    ImGui::SetNextItemWidth(unit * 5.5f);
    changed |= ImGui::InputInt("##d", &date.day);
    ImGui::SameLine();
    std::string label = VisibleLabel(id);
    ImGui::TextUnformatted(label.empty() ? "Y / M / D" : label.c_str());
    ImGui::PopID();

    // Keep the date valid at all times.
    date.year = Validation::ClampYear(date.year);
    if (date.month < 1) date.month = 12;
    if (date.month > 12) date.month = 1;
    int dim = Utils::DaysInMonth(date.year, date.month);
    if (date.day < 1) date.day = dim;
    if (date.day > dim) date.day = (changed && date.day == dim + 1) ? 1 : dim;
    return changed;
}

bool TimeInput(const char* id, int& minutes) {
    ImGui::PushID(id);
    const float unit = ImGui::GetFontSize();
    int h = minutes / 60, m = minutes % 60;
    bool changed = false;
    ImGui::SetNextItemWidth(unit * 5.5f);
    changed |= ImGui::InputInt("##h", &h);
    ImGui::SameLine(0, 4);
    ImGui::TextUnformatted(":");
    ImGui::SameLine(0, 4);
    ImGui::SetNextItemWidth(unit * 5.5f);
    changed |= ImGui::InputInt("##m", &m, 5, 15);
    ImGui::SameLine();
    std::string label = VisibleLabel(id);
    ImGui::TextUnformatted(label.empty() ? "HH : MM" : label.c_str());
    ImGui::PopID();
    if (m >= 60) { m = 0; h++; }
    if (m < 0) { m = 55; h--; }
    if (h > 23) h = 0;
    if (h < 0) h = 23;
    minutes = h * 60 + m;
    return changed;
}

bool CurrencyCombo(const char* label, std::string& code, const std::vector<std::string>& options, float width) {
    static std::unordered_map<ImGuiID, std::string> customBuffers;
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(label, code.c_str())) {
        for (auto& o : options) {
            if (ImGui::Selectable(o.c_str(), o == code)) { code = o; changed = true; }
        }
        ImGui::Separator();
        std::string& custom = customBuffers[ImGui::GetID("##custom")];
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.0f);
        ImGui::InputTextWithHint("##custom", "Other", &custom,
                                 ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank);
        ImGui::SameLine();
        std::string normalized;
        bool valid = Validation::NormalizeCurrency(custom, normalized);
        ImGui::BeginDisabled(!valid);
        if (ImGui::Button("Use")) {
            code = normalized;
            custom.clear();
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::EndCombo();
    }
    return changed;
}

bool CategoryInput(const char* label, std::string& category, const std::vector<std::string>& options, float width) {
    ImGui::PushID(label);
    const float arrow = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    ImGui::SetNextItemWidth(std::max(40.0f, width - arrow - spacing));
    bool changed = ImGui::InputTextWithHint("##text", "General", &category);
    ImGui::SameLine(0, spacing);
    if (ImGui::ArrowButton("##list", ImGuiDir_Down)) ImGui::OpenPopup("##catlist");
    if (ImGui::BeginPopup("##catlist")) {
        if (options.empty()) ImGui::TextDisabled("No categories yet");
        for (auto& o : options) {
            if (ImGui::Selectable(o.c_str(), Utils::EqualsCI(o, category))) { category = o; changed = true; }
        }
        ImGui::EndPopup();
    }
    std::string visible = VisibleLabel(label);
    if (!visible.empty()) {
        ImGui::SameLine(0, spacing);
        ImGui::TextUnformatted(visible.c_str());
    }
    ImGui::PopID();
    return changed;
}

bool CommitDoubleInput(const char* id, double modelValue, double& committed, float width, const char* format) {
    static std::unordered_map<ImGuiID, double> editing;
    ImGuiID key = ImGui::GetID(id);
    auto it = editing.find(key);
    double v = it != editing.end() ? it->second : modelValue;
    ImGui::SetNextItemWidth(width);
    if (ImGui::InputDouble(id, &v, 0.0, 0.0, format)) editing[key] = v;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        auto jt = editing.find(key);
        committed = jt != editing.end() ? jt->second : v;
        editing.erase(key);
        return true;
    }
    if (!ImGui::IsItemActive()) editing.erase(key);
    return false;
}

void ErrorText(const std::string& message) {
    if (message.empty()) return;
    ImGui::PushStyleColor(ImGuiCol_Text, ThemeManager::Tokens().danger);
    ImGui::TextWrapped("%s", message.c_str());
    ImGui::PopStyleColor();
}

void MutedText(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, ThemeManager::Tokens().muted);
    ImGui::TextV(fmt, args);
    ImGui::PopStyleColor();
    va_end(args);
}

void Badge(const char* text, const ImVec4& color) {
    ImVec2 pad(ImGui::GetFontSize() * 0.35f, 1.0f);
    ImVec2 size = ImGui::CalcTextSize(text);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec4 bg = color;
    bg.w = 0.18f;
    ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + size.x + pad.x * 2, pos.y + size.y + pad.y * 2),
                                              ImGui::GetColorU32(bg), 4.0f);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + pad.x, pos.y + pad.y));
    ImGui::TextColored(color, "%s", text);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + size.x + pad.x * 2, pos.y));
    ImGui::Dummy(ImVec2(0, size.y + pad.y * 2));
}

void ColoredProgressBar(float fraction, const ImVec4& color, const ImVec2& size, const char* overlay) {
    if (!(fraction >= 0.0f)) fraction = 0.0f;   // also catches NaN
    if (fraction > 1.0f) fraction = 1.0f;
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
    ImGui::ProgressBar(fraction, size, overlay);
    ImGui::PopStyleColor();
}

void KpiCard(const char* id, const char* title, const std::string& value, const ImVec4& valueColor,
             float width, float height, const char* subtitle, float progress, const ImVec4* progressColor) {
    const ThemeTokens& t = ThemeManager::Tokens();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, t.cardBg);
    ImGui::BeginChild(id, ImVec2(width, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    ImGui::TextColored(t.muted, "%s", title);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.45f);
    ImGui::TextColored(valueColor, "%s", value.c_str());
    ImGui::PopFont();
    if (progress >= 0.0f) {
        ColoredProgressBar(progress, progressColor ? *progressColor : t.accent,
                           ImVec2(-FLT_MIN, ImGui::GetFontSize() * 0.5f), "");
    }
    if (subtitle) ImGui::TextColored(t.muted, "%s", subtitle);
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

bool DangerButton(const char* label, const ImVec2& size) {
    const ThemeTokens& t = ThemeManager::Tokens();
    ImGui::PushStyleColor(ImGuiCol_Button, t.dangerButton);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.dangerButtonHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, t.dangerButtonHover);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return pressed;
}

bool SubmitShortcutPressed() {
    return ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
}

double NiceCeil(double value) {
    if (!(value > 0.0)) return 100.0;
    double mag = std::pow(10.0, std::floor(std::log10(value)));
    double n = value / mag;
    double nice = n <= 1.0 ? 1.0 : n <= 2.0 ? 2.0 : n <= 2.5 ? 2.5 : n <= 5.0 ? 5.0 : 10.0;
    return nice * mag;
}

int CompactMoneyFormatter(double value, char* buff, int size, void* userData) {
    const char* code = userData ? static_cast<const char*>(userData) : "";
    std::string sym = MoneyUtil::CurrencySymbol(code);
    double a = std::fabs(value);
    double shown = a;
    const char* suffix = "";
    if (std::string(code) == "INR") {
        if (a >= 1e7)      { shown = a / 1e7; suffix = "Cr"; }
        else if (a >= 1e5) { shown = a / 1e5; suffix = "L"; }
        else if (a >= 1e3) { shown = a / 1e3; suffix = "k"; }
    } else {
        if (a >= 1e9)      { shown = a / 1e9; suffix = "B"; }
        else if (a >= 1e6) { shown = a / 1e6; suffix = "M"; }
        else if (a >= 1e3) { shown = a / 1e3; suffix = "k"; }
    }
    int decimals = (std::fabs(shown - std::round(shown)) < 0.05) ? 0 : 1;
    return std::snprintf(buff, static_cast<size_t>(size), "%s%s%.*f%s", value < 0 ? "-" : "", sym.c_str(),
                         decimals, shown, suffix);
}

#ifdef _WIN32
static HWND ActiveWindow() {
    return GetActiveWindow();
}

bool OpenFileDialog(const wchar_t* filter, std::filesystem::path& out, const std::filesystem::path& initialDir) {
    std::wstring buf(32768, L'\0');
    std::wstring dir = initialDir.wstring();
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = ActiveWindow();
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = static_cast<DWORD>(buf.size());
    ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return false;
    out = std::filesystem::path(buf.c_str());
    return true;
}

bool SaveFileDialog(const wchar_t* filter, const wchar_t* defaultExt, const std::wstring& defaultName,
                    std::filesystem::path& out) {
    std::wstring buf(32768, L'\0');
    buf.replace(0, defaultName.size(), defaultName);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = ActiveWindow();
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = static_cast<DWORD>(buf.size());
    ofn.lpstrDefExt = defaultExt;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetSaveFileNameW(&ofn)) return false;
    out = std::filesystem::path(buf.c_str());
    return true;
}
void OpenFolder(const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
#else
void OpenFolder(const std::filesystem::path&) {}
bool OpenFileDialog(const wchar_t*, std::filesystem::path&, const std::filesystem::path&) { return false; }
bool SaveFileDialog(const wchar_t*, const wchar_t*, const std::wstring&, std::filesystem::path&) { return false; }
#endif

} // namespace Ui
