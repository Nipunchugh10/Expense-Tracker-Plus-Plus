#include "Tabs/DashboardTab.h"
#include "ExpenseTracker.h"
#include "ThemeManager.h"
#include "UiHelpers.h"
#include "Utils.h"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

// ImPlotFormatter for pie slices: "32%" on slices of at least 4%, blank otherwise.
int PieSliceLabel(double value, char* buff, int size, void* userData) {
    double total = *static_cast<const double*>(userData);
    double pct = total > 0.0 ? value * 100.0 / total : 0.0;
    if (pct < 4.0) {
        if (size > 0) buff[0] = '\0';
        return 0;
    }
    return std::snprintf(buff, static_cast<size_t>(size), "%.0f%%", pct);
}

} // namespace

void DashboardTab::Render(AppContext& ctx) {
    const ExpenseTracker& tracker = *ctx.tracker;
    const ThemeTokens& tk = ThemeManager::Tokens();
    const std::string& base = tracker.GetBaseCurrency();
    const float u = ImGui::GetFontSize();
    if (selectedYear == 0) {
        selectedYear = ctx.today.year;
        selectedMonth = ctx.today.month;
    }

    ImGui::Spacing();
    Ui::YearInput("Year", selectedYear, u * 7);
    ImGui::SameLine();
    Ui::MonthCombo("Month", selectedMonth, true, u * 9);

    ImGui::SameLine(ImGui::GetContentRegionMax().x - u * 10);
    ImGui::BeginDisabled(ctx.readOnly);
    if (Ui::DangerButton("Reset Everything", ImVec2(u * 10, 0))) openResetConfirm = true;
    ImGui::EndDisabled();

    if (openResetConfirm) {
        ImGui::OpenPopup("Reset All Data?");
        openResetConfirm = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Reset All Data?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("This deletes ALL data:");
        ImGui::BulletText("All transactions");
        ImGui::BulletText("All subscriptions");
        ImGui::BulletText("All budgets and goals");
        ImGui::BulletText("Custom exchange rates");
        ImGui::Spacing();
        Ui::MutedText("A timestamped backup of the current data is written first.");
        ImGui::Spacing();
        if (Ui::DangerButton("Yes, reset everything", ImVec2(u * 12, 0))) {
            if (ctx.requestReset) ctx.requestReset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(u * 7, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();
    ImGui::Spacing();

    // Mixed-currency guard (P0-C5)
    int excluded = tracker.CountUnconvertible(selectedYear, selectedMonth);
    if (excluded > 0) {
        ImGui::TextColored(tk.warning,
                           "%d transaction(s) use a currency without an exchange rate and are not included below. "
                           "Add the rate under Tools > Exchange Rates.", excluded);
        ImGui::Spacing();
    }

    // ── Cash-flow KPIs (Step 4) ──
    Totals income = tracker.GetTotal(selectedYear, selectedMonth, TransactionType::Income);
    Totals expense = tracker.GetTotal(selectedYear, selectedMonth, TransactionType::Expense);
    Money net = income.amount - expense.amount;
    auto rate = tracker.GetSavingsRate(selectedYear, selectedMonth);

    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float cardW = (ImGui::GetContentRegionAvail().x - gap * 3) / 4.0f;
    const float cardH = u * 5.2f;
    std::string period = selectedMonth > 0 ? std::string(Ui::kMonthNames[selectedMonth - 1]) + " " + std::to_string(selectedYear)
                                           : "Year " + std::to_string(selectedYear);

    Ui::KpiCard("##kpiIncome", "Total Income", MoneyUtil::Format(income.amount, base), tk.income, cardW, cardH, period.c_str());
    ImGui::SameLine();
    Ui::KpiCard("##kpiExpense", "Total Expenses", MoneyUtil::Format(expense.amount, base), tk.expense, cardW, cardH, period.c_str());
    ImGui::SameLine();
    Ui::KpiCard("##kpiNet", "Net Cash Flow", MoneyUtil::Format(net, base), net >= 0 ? tk.success : tk.danger, cardW, cardH,
                net >= 0 ? "Surplus" : "Deficit");
    ImGui::SameLine();
    if (rate) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f%%", *rate);
        float fraction = static_cast<float>(std::clamp(*rate, 0.0, 100.0) / 100.0);
        const ImVec4& color = *rate < 0 ? tk.danger : *rate < 10 ? tk.warning : tk.success;
        Ui::KpiCard("##kpiRate", "Savings Rate", buf, color, cardW, cardH, nullptr, fraction, &color);
    } else {
        Ui::KpiCard("##kpiRate", "Savings Rate", "N/A", tk.muted, cardW, cardH, "No income recorded");
    }

    if (selectedMonth > 0 && tracker.GetBudgetManager().HasOverallBudget(selectedYear, selectedMonth)) {
        Money budget = tracker.GetBudgetManager().GetOverallBudget(selectedYear, selectedMonth);
        Money left = budget - expense.amount;
        ImGui::TextColored(left >= 0 ? tk.success : tk.danger, "Monthly budget: %s of %s %s",
                           MoneyUtil::Format(left >= 0 ? left : -left, base).c_str(),
                           MoneyUtil::Format(budget, base).c_str(), left >= 0 ? "remaining" : "OVER");
    }

    // Subscription reminders: shown whether auto-record is on or off.
    {
        const int nowMin = Date::NowMinutes();
        std::vector<std::pair<long long, std::string>> soon;
        int toRecord = 0;
        for (auto& r : tracker.GetRecurringRules()) {
            toRecord += static_cast<int>(tracker.GetUnrecordedRenewals(r, ctx.today, nowMin, 1).size());
            Date next;
            if (!r.IsActive() || !r.NextRenewal(ctx.today, nowMin, next)) continue;
            long long days = Utils::DaysBetween(ctx.today, next);
            if (days > 7) continue;
            std::string when = days == 0 ? "today at " + r.TimeString()
                             : std::to_string(days) + (days == 1 ? " day" : " days");
            soon.emplace_back(days, r.GetDescription() + " (" + when + ")");
        }
        std::sort(soon.begin(), soon.end());
        if (!soon.empty()) {
            std::string list;
            for (size_t i = 0; i < soon.size(); i++) list += (i ? ", " : "") + soon[i].second;
            ImGui::TextColored(tk.warning, "Renewing within 7 days: %s", list.c_str());
        }
        if (toRecord > 0) {
            ImGui::TextColored(tk.danger, "%d subscription payment(s) are due and not recorded yet - see the Subscriptions tab.",
                               toRecord);
        }
    }

    ImGui::Spacing();

    // ── Charts ──
    const float halfW = (ImGui::GetContentRegionAvail().x - gap) / 2.0f;
    const float chartH = std::max(u * 16.0f, ImGui::GetContentRegionAvail().y * 0.52f);

    ImGui::BeginChild("##PieSection", ImVec2(halfW, chartH), ImGuiChildFlags_Borders);
    ImGui::Text("Spending by category (%s)", period.c_str());
    auto breakdown = tracker.GetCategoryBreakdown(selectedYear, selectedMonth);
    if (!breakdown.empty()) {
        std::vector<std::pair<std::string, Money>> sorted(breakdown.begin(), breakdown.end());
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });

        // The default colormap has 10 colours: keep the 9 largest categories and
        // fold the rest into "Other" so no two slices share a colour.
        constexpr size_t kMaxSlices = 9;
        if (sorted.size() > kMaxSlices + 1) {
            Money other = 0;
            for (size_t i = kMaxSlices; i < sorted.size(); i++) other += sorted[i].second;
            sorted.resize(kMaxSlices);
            sorted.emplace_back("Other", other);
        }
        Money total = 0;
        for (auto& [cat, v] : sorted) total += v;

        // Legend entries carry the percentage; "###" keeps each item's ID stable
        // when the percentage changes, so hidden/shown toggles survive edits.
        std::vector<std::string> labels;
        std::vector<double> values;
        for (auto& [cat, v] : sorted) {
            double pct = total > 0 ? static_cast<double>(v) * 100.0 / static_cast<double>(total) : 0.0;
            char pctText[16];
            std::snprintf(pctText, sizeof(pctText), "%.1f%%", pct);
            labels.push_back(cat + "  " + pctText + "###" + cat);
            values.push_back(MoneyUtil::ToMajor(v));
        }
        std::vector<const char*> labelPtrs;
        for (auto& l : labels) labelPtrs.push_back(l.c_str());
        double totalMajor = MoneyUtil::ToMajor(total);

        // Equal aspect keeps the pie circular; limits are only a starting point
        // (forcing them every frame would override the aspect and stretch it).
        if (ImPlot::BeginPlot("##PieChart", ImVec2(-1, -1),
                              ImPlotFlags_Equal | ImPlotFlags_NoMouseText | ImPlotFlags_NoInputs | ImPlotFlags_NoFrame)) {
            ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations | ImPlotAxisFlags_AutoFit,
                              ImPlotAxisFlags_NoDecorations | ImPlotAxisFlags_AutoFit);
            ImPlot::SetupLegend(ImPlotLocation_East, ImPlotLegendFlags_Outside);
            ImPlotSpec spec;
            spec.Flags = ImPlotPieChartFlags_Normalize | ImPlotPieChartFlags_Exploding;
            // Percent written on slices of at least 4%; thinner slices rely on the legend.
            ImPlot::PlotPieChart(labelPtrs.data(), values.data(), static_cast<int>(values.size()), 0.5, 0.5, 0.42,
                                 PieSliceLabel, &totalMajor, 90, spec);
            ImPlot::EndPlot();
        }
    } else {
        ImGui::TextDisabled("No expenses for this period.");
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##BarSection", ImVec2(halfW, chartH), ImGuiChildFlags_Borders);
    ImGui::Text("Income vs expenses by month (%d)", selectedYear);
    auto expenses = tracker.GetMonthlyTotals(selectedYear, TransactionType::Expense);
    auto incomes = tracker.GetMonthlyTotals(selectedYear, TransactionType::Income);
    double expData[12], incData[12], positions[12];
    double maxVal = 0;
    for (int i = 0; i < 12; i++) {
        expData[i] = MoneyUtil::ToMajor(expenses[static_cast<size_t>(i)]);
        incData[i] = MoneyUtil::ToMajor(incomes[static_cast<size_t>(i)]);
        positions[i] = i;
        maxVal = std::max({maxVal, expData[i], incData[i]});
    }
    if (ImPlot::BeginPlot("##BarChart", ImVec2(-1, -1))) {
        ImPlot::SetupAxes(nullptr, nullptr);
        ImPlot::SetupAxisTicks(ImAxis_X1, positions, 12, Ui::kMonthShort);
        ImPlot::SetupAxisLimits(ImAxis_X1, -0.6, 11.6, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, Ui::NiceCeil(maxVal * 1.1), ImPlotCond_Always);
        ImPlot::SetupAxisFormat(ImAxis_Y1, Ui::CompactMoneyFormatter, const_cast<char*>(base.c_str()));
        ImPlot::SetupLegend(ImPlotLocation_NorthWest);
        ImPlotSpec expSpec;
        expSpec.FillColor = tk.expense;
        expSpec.LineColor = tk.expense;
        ImPlot::PlotBars("Expenses", expData, 12, 0.38, -0.2, expSpec);
        ImPlotSpec incSpec;
        incSpec.FillColor = tk.income;
        incSpec.LineColor = tk.income;
        ImPlot::PlotBars("Income", incData, 12, 0.38, 0.2, incSpec);
        ImPlot::EndPlot();
    }
    ImGui::EndChild();

    ImGui::Spacing();

    // ── Recent transactions ──
    ImGui::Text("Recent transactions");
    std::vector<const Expense*> recent = tracker.GetRecentTransactions(10);   // cached per change

    if (recent.empty()) {
        ImGui::TextDisabled("No transactions yet. Add some from the Expenses tab.");
    } else if (ImGui::BeginTable("##Recent", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
                                 ImVec2(0, std::max(u * 6.0f, ImGui::GetContentRegionAvail().y - 4)))) {
        ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Amount", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (auto* e : recent) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(e->GetDate().ToString().c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(e->GetDescription().c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(e->GetCategory().c_str());
            ImGui::TableSetColumnIndex(3);
            std::string amount = MoneyUtil::Format(e->GetAmount(), e->GetCurrency());
            if (e->IsIncome()) ImGui::TextColored(tk.income, "+%s", amount.c_str());
            else if (e->IsSpending()) ImGui::TextColored(tk.expense, "-%s", amount.c_str());
            else ImGui::TextColored(tk.transfer, "%s", amount.c_str());
            ImGui::TableSetColumnIndex(4);
            Ui::MutedText("%s", TransactionTypeLabel(e->GetType()));
        }
        ImGui::EndTable();
    }
}
