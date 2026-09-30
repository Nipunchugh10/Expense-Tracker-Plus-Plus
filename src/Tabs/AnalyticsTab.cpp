#include "Tabs/AnalyticsTab.h"
#include "ExpenseTracker.h"
#include "ThemeManager.h"
#include "UiHelpers.h"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

std::string Ordinal(int n) {
    const char* suffix = "th";
    if (n % 100 < 11 || n % 100 > 13) {
        switch (n % 10) {
            case 1: suffix = "st"; break;
            case 2: suffix = "nd"; break;
            case 3: suffix = "rd"; break;
            default: break;
        }
    }
    return std::to_string(n) + suffix;
}

} // namespace

void AnalyticsTab::Render(AppContext& ctx) {
    const float u = ImGui::GetFontSize();
    if (selectedYear == 0) {
        selectedYear = ctx.today.year;
        forecastMonth = ctx.today.month;
    }
    ImGui::Spacing();
    Ui::YearInput("Year", selectedYear, u * 7);
    ImGui::SameLine();
    Ui::MonthCombo("Forecast month", forecastMonth, false, u * 9);
    ImGui::SameLine();
    Ui::MutedText("All amounts in %s.", ctx.tracker->GetBaseCurrency().c_str());
    ImGui::Separator();

    ImGui::BeginChild("##AnalyticsScroll", ImVec2(0, 0), ImGuiChildFlags_None);
    RenderForecast(ctx);
    ImGui::Spacing();
    const float halfW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
    ImGui::BeginChild("##AnalyticsLeft", ImVec2(halfW, u * 34), ImGuiChildFlags_Borders);
    RenderBreakdowns(ctx);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##AnalyticsRight", ImVec2(halfW, u * 34), ImGuiChildFlags_Borders);
    RenderTrendAndTop(ctx);
    ImGui::EndChild();
    ImGui::EndChild();
}

void AnalyticsTab::RenderForecast(AppContext& ctx) {
    const ExpenseTracker& tracker = *ctx.tracker;
    const ThemeTokens& tk = ThemeManager::Tokens();
    const std::string& base = tracker.GetBaseCurrency();
    const float u = ImGui::GetFontSize();
    const int y = selectedYear, m = forecastMonth;

    ForecastResult f = tracker.ComputeForecast(y, m, ctx.today);
    ImGui::Text("Month-end run-rate forecast - %s %d", Ui::kMonthNames[m - 1], y);

    if (f.kind == ForecastResult::Kind::Future) {
        Ui::MutedText("This month has not started yet. Scheduled bills so far: %s.",
                      MoneyUtil::Format(f.scheduledRemaining, base).c_str());
    }

    // Stats strip
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float w = (ImGui::GetContentRegionAvail().x - gap * 3) / 4.0f;
    const float h = u * 4.4f;
    Ui::KpiCard("##fSpent", f.kind == ForecastResult::Kind::Past ? "Spent (whole month)" : "Spent so far",
                MoneyUtil::Format(f.spentToDate, base), tk.text, w, h);
    ImGui::SameLine();
    Ui::KpiCard("##fVel", "Discretionary burn / day", MoneyUtil::Format(f.dailyVelocity, base), tk.accent, w, h,
                "excludes recurring bills");
    ImGui::SameLine();
    Ui::KpiCard("##fSched", "Scheduled bills remaining", MoneyUtil::Format(f.scheduledRemaining, base), tk.warning, w, h);
    ImGui::SameLine();
    bool over = f.hasBudget && f.projectedTotal > f.budget;
    Ui::KpiCard("##fProj", f.kind == ForecastResult::Kind::Past ? "Month total" : "Projected month-end",
                MoneyUtil::Format(f.projectedTotal, base), over ? tk.danger : tk.success, w, h,
                f.hasBudget ? ("Budget " + MoneyUtil::Format(f.budget, base)).c_str() : "No budget set");

    // Warning card
    if (f.hasBudget && f.exhaustDay > 0) {
        ImVec4 bg = tk.danger;
        bg.w = 0.14f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
        ImGui::BeginChild("##fWarn", ImVec2(0, u * 2.4f), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
        if (f.alreadyExceeded) {
            ImGui::TextColored(tk.danger, "Warning: the budget of %s was exceeded on the %s.",
                               MoneyUtil::Format(f.budget, base).c_str(), Ordinal(f.exhaustDay).c_str());
        } else {
            ImGui::TextColored(tk.danger, "Warning: at your current burn rate of %s/day, you will exhaust your budget by the %s.",
                               MoneyUtil::Format(f.dailyVelocity, base).c_str(), Ordinal(f.exhaustDay).c_str());
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    } else if (f.hasBudget && f.kind != ForecastResult::Kind::Past) {
        ImGui::TextColored(tk.success, "On track: projected %s of the %s budget.", MoneyUtil::Format(f.projectedTotal, base).c_str(),
                           MoneyUtil::Format(f.budget, base).c_str());
    }
    if (f.excluded > 0) {
        ImGui::TextColored(tk.warning, "%d item(s) without an exchange rate are not included.", f.excluded);
    }

    if (f.daysInMonth == 0 || ImGui::GetContentRegionAvail().x < 50) return;

    double maxY = f.hasBudget ? MoneyUtil::ToMajor(f.budget) : 0.0;
    for (double v : f.actualCumulative) maxY = std::max(maxY, v);
    for (double v : f.forecastHigh) maxY = std::max(maxY, v);

    if (ImPlot::BeginPlot("##Forecast", ImVec2(-1, u * 18))) {
        ImPlot::SetupAxes("Day of month", nullptr);
        ImPlot::SetupAxisLimits(ImAxis_X1, 1, f.daysInMonth, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, Ui::NiceCeil(maxY * 1.1), ImPlotCond_Always);
        ImPlot::SetupAxisFormat(ImAxis_Y1, Ui::CompactMoneyFormatter, const_cast<char*>(base.c_str()));
        ImPlot::SetupLegend(ImPlotLocation_NorthWest);

        if (!f.forecastDays.empty()) {
            ImPlotSpec cone;
            cone.FillColor = tk.warning;
            cone.FillAlpha = 0.18f;
            ImPlot::PlotShaded("Projection range", f.forecastDays.data(), f.forecastLow.data(), f.forecastHigh.data(),
                               static_cast<int>(f.forecastDays.size()), cone);
            ImPlotSpec line;
            line.LineColor = tk.warning;
            line.LineWeight = 2.0f;
            line.Marker = ImPlotMarker_Circle;
            line.MarkerSize = 2.5f;
            ImPlot::PlotLine("Forecast", f.forecastDays.data(), f.forecastCumulative.data(),
                             static_cast<int>(f.forecastDays.size()), line);
        }
        if (!f.actualCumulative.empty()) {
            ImPlotSpec actual;
            actual.LineColor = tk.accent;
            actual.LineWeight = 2.5f;
            ImPlot::PlotLine("Actual", f.days.data(), f.actualCumulative.data(), static_cast<int>(f.actualCumulative.size()),
                             actual);
        }
        if (f.hasBudget) {
            double b = MoneyUtil::ToMajor(f.budget);
            ImPlotSpec ceiling;
            ceiling.LineColor = tk.danger;
            ceiling.LineWeight = 2.0f;
            ceiling.Flags = ImPlotInfLinesFlags_Horizontal;
            ImPlot::PlotInfLines("Budget", &b, 1, ceiling);
        }
        ImPlot::EndPlot();
    }
}

void AnalyticsTab::RenderBreakdowns(AppContext& ctx) {
    const ExpenseTracker& tracker = *ctx.tracker;
    const ThemeTokens& tk = ThemeManager::Tokens();
    const std::string& base = tracker.GetBaseCurrency();
    const int y = selectedYear;

    ImGui::Text("Spending by category (%d)", y);
    auto breakdown = tracker.GetCategoryBreakdown(y);
    Money total = tracker.GetYearlyTotal(y);
    std::vector<std::pair<std::string, Money>> sorted(breakdown.begin(), breakdown.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });

    if (ImGui::BeginTable("##CatBreakdown", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Total");
        ImGui::TableSetupColumn("Share");
        ImGui::TableHeadersRow();
        for (auto& [cat, v] : sorted) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(cat.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(MoneyUtil::Format(v, base).c_str());
            ImGui::TableSetColumnIndex(2);
            if (total > 0) ImGui::Text("%.1f%%", static_cast<double>(v) / static_cast<double>(total) * 100.0);
        }
        if (sorted.empty()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("No expenses this year.");
        } else {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(tk.accent, "Total");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(tk.accent, "%s", MoneyUtil::Format(total, base).c_str());
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Text("Income by category (%d)", y);
    auto incomeBreakdown = tracker.GetCategoryBreakdown(y, 0, TransactionType::Income);
    if (incomeBreakdown.empty()) {
        ImGui::TextDisabled("No income recorded this year.");
    } else if (ImGui::BeginTable("##IncBreakdown", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Total");
        ImGui::TableHeadersRow();
        for (auto& [cat, v] : incomeBreakdown) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(cat.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(tk.income, "%s", MoneyUtil::Format(v, base).c_str());
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Text("Expenses by currency (%d)", y);
    auto currencies = tracker.GetCurrencyBreakdown(y);
    if (ImGui::BeginTable("##CurBreakdown", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Currency");
        ImGui::TableSetupColumn("Original amount", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("In base currency");
        ImGui::TableHeadersRow();
        for (auto& [code, amount] : currencies) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(code.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(MoneyUtil::Format(amount, code).c_str());
            ImGui::TableSetColumnIndex(2);
            Money converted;
            if (tracker.GetCurrency().ConvertToBase(amount, code, converted)) {
                ImGui::TextUnformatted(MoneyUtil::Format(converted, base).c_str());
            } else {
                ImGui::TextColored(tk.warning, "no rate");
            }
        }
        ImGui::EndTable();
    }
}

void AnalyticsTab::RenderTrendAndTop(AppContext& ctx) {
    const ExpenseTracker& tracker = *ctx.tracker;
    const ThemeTokens& tk = ThemeManager::Tokens();
    const std::string& base = tracker.GetBaseCurrency();
    const float u = ImGui::GetFontSize();
    const int y = selectedYear;

    ImGui::Text("Monthly trend (%d)", y);
    auto expenses = tracker.GetMonthlyTotals(y, TransactionType::Expense);
    auto incomes = tracker.GetMonthlyTotals(y, TransactionType::Income);
    double expData[12], incData[12], positions[12];
    double maxVal = 0;
    for (int i = 0; i < 12; i++) {
        expData[i] = MoneyUtil::ToMajor(expenses[static_cast<size_t>(i)]);
        incData[i] = MoneyUtil::ToMajor(incomes[static_cast<size_t>(i)]);
        positions[i] = i;
        maxVal = std::max({maxVal, expData[i], incData[i]});
    }
    if (ImPlot::BeginPlot("##Trend", ImVec2(-1, u * 15))) {
        ImPlot::SetupAxes(nullptr, nullptr);
        ImPlot::SetupAxisTicks(ImAxis_X1, positions, 12, Ui::kMonthShort);
        ImPlot::SetupAxisLimits(ImAxis_X1, -0.6, 11.6, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, Ui::NiceCeil(maxVal * 1.1), ImPlotCond_Always);
        ImPlot::SetupAxisFormat(ImAxis_Y1, Ui::CompactMoneyFormatter, const_cast<char*>(base.c_str()));
        ImPlot::SetupLegend(ImPlotLocation_NorthWest);
        ImPlotSpec bars;
        bars.FillColor = tk.expense;
        bars.LineColor = tk.expense;
        ImPlot::PlotBars("Expenses", expData, 12, 0.6, 0.0, bars);
        ImPlotSpec line;
        line.LineColor = tk.income;
        line.LineWeight = 2.0f;
        line.Marker = ImPlotMarker_Circle;
        line.MarkerSize = 3.0f;
        ImPlot::PlotLine("Income", positions, incData, 12, line);
        ImPlot::EndPlot();
    }

    ImGui::Spacing();
    ImGui::Text("Top 10 largest expenses (%d)", y);
    auto top = tracker.GetTopExpenses(y, 10);
    if (top.empty()) {
        ImGui::TextDisabled("No expenses this year.");
        return;
    }
    if (ImGui::BeginTable("##Top10", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("#");
        ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Amount");
        ImGui::TableSetupColumn("Date");
        ImGui::TableHeadersRow();
        int rank = 1;
        for (auto* e : top) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", rank++);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(e->GetDescription().c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(MoneyUtil::Format(e->GetAmount(), e->GetCurrency()).c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(e->GetDate().ToString().c_str());
        }
        ImGui::EndTable();
    }
}
