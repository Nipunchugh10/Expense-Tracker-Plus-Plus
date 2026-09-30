#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "Expense.h"
#include "imgui.h"

namespace Ui {
    extern const char* const kMonthNames[12];
    extern const char* const kMonthShort[12];

    // Year selector clamped to the supported range.
    bool YearInput(const char* label, int& year, float width);
    // month 1-12, or 0 = "All months" when allowAll.
    bool MonthCombo(const char* label, int& month, bool allowAll, float width);
    // Year / month / day inputs; the day is clamped to the month length.
    bool DateInput(const char* id, Date& date);
    // Hour / minute inputs for a time of day stored as minutes (0..1439).
    bool TimeInput(const char* id, int& minutes);

    // Combo listing known codes plus free entry of another 3-letter code.
    bool CurrencyCombo(const char* label, std::string& code, const std::vector<std::string>& options, float width);
    // Text input with a dropdown of existing categories.
    bool CategoryInput(const char* label, std::string& category, const std::vector<std::string>& options, float width);

    // A double input that reports a value only once the edit is finished
    // (focus lost or Enter), so partially typed numbers are never committed.
    bool CommitDoubleInput(const char* id, double modelValue, double& committed, float width, const char* format = "%.2f");

    void ErrorText(const std::string& message);
    void MutedText(const char* fmt, ...) IM_FMTARGS(1);
    void Badge(const char* text, const ImVec4& color);
    void KpiCard(const char* id, const char* title, const std::string& value, const ImVec4& valueColor,
                 float width, float height, const char* subtitle = nullptr, float progress = -1.0f,
                 const ImVec4* progressColor = nullptr);
    void ColoredProgressBar(float fraction, const ImVec4& color, const ImVec2& size, const char* overlay);

    bool DangerButton(const char* label, const ImVec2& size = ImVec2(0, 0));
    bool SubmitShortcutPressed();   // Enter / keypad Enter, once per press

    // Axis helpers for money charts.
    double NiceCeil(double value);
    int CompactMoneyFormatter(double value, char* buff, int size, void* userData);   // ImPlotFormatter

    // Native Unicode file dialogs (P0-A8). Filters use the Win32 "Name\0*.ext\0\0" form.
    bool OpenFileDialog(const wchar_t* filter, std::filesystem::path& out,
                        const std::filesystem::path& initialDir = std::filesystem::path());
    bool SaveFileDialog(const wchar_t* filter, const wchar_t* defaultExt, const std::wstring& defaultName,
                        std::filesystem::path& out);
    void OpenFolder(const std::filesystem::path& dir);
}
