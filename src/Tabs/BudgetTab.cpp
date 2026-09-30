#include "Tabs/BudgetTab.h"
#include "ExpenseTracker.h"
#include "ThemeManager.h"
#include "UiHelpers.h"
#include "Utils.h"
#include "Validation.h"
#include "imgui.h"
#include <algorithm>
#include <cstdio>
#include <map>

namespace {

enum class Health { NoBudget, Safe, Warning, Over };

// Exact integer thresholds: Safe < 75%, Warning 75-99%, Over >= 100% (Step 3).
Health Classify(Money spent, Money limit) {
    if (limit <= 0) return Health::NoBudget;
    if (spent >= limit) return Health::Over;
    if (spent * 100 >= limit * 75) return Health::Warning;
    return Health::Safe;
}

void HealthTag(Health h, Money spent, Money limit, const std::string& base) {
    const ThemeTokens& tk = ThemeManager::Tokens();
    switch (h) {
        case Health::NoBudget: Ui::Badge("No budget", tk.muted); break;
        case Health::Safe:     Ui::Badge("Safe", tk.success); break;
        case Health::Warning:  Ui::Badge("Warning", tk.warning); break;
        case Health::Over:
            if (spent == limit) {
                Ui::Badge("At limit", tk.danger);
            } else {
                std::string text = "Over by " + MoneyUtil::Format(spent - limit, base);
                Ui::Badge(text.c_str(), tk.danger);
            }
            break;
    }
}

ImVec4 HealthColor(Health h) {
    const ThemeTokens& tk = ThemeManager::Tokens();
    switch (h) {
        case Health::Safe:    return tk.success;
        case Health::Warning: return tk.warning;
        case Health::Over:    return tk.danger;
        case Health::NoBudget: break;
    }
    return tk.muted;
}

} // namespace

void BudgetTab::Render(AppContext& ctx) {
    const ExpenseTracker& tracker = *ctx.tracker;
    const ThemeTokens& tk = ThemeManager::Tokens();
    const std::string& base = tracker.GetBaseCurrency();
    const BudgetManager& bm = tracker.GetBudgetManager();
    const float u = ImGui::GetFontSize();
    if (selectedYear == 0) {
        selectedYear = ctx.today.year;
        selectedMonth = ctx.today.month;
    }

    ImGui::Spacing();
    Ui::YearInput("Year", selectedYear, u * 7);
    ImGui::SameLine();
    Ui::MonthCombo("Month", selectedMonth, false, u * 9);
    ImGui::SameLine();
    Ui::MutedText("All budgets are in the base currency (%s).", base.c_str());
    const int y = selectedYear, m = selectedMonth;

    ImGui::Separator();
    ImGui::Spacing();

    // ── Overall monthly budget ──
    Money overall = bm.GetOverallBudget(y, m);
    Money spentTotal = tracker.GetMonthlyTotal(y, m);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Overall budget for %s %d", Ui::kMonthNames[m - 1], y);
    ImGui::SameLine(u * 16);
    double committed;
    ImGui::BeginDisabled(ctx.readOnly);
    if (Ui::CommitDoubleInput("##overall", MoneyUtil::ToMajor(overall), committed, u * 9)) {
        Money limit;
        std::string err;
        if (Validation::ValidateAmount(committed, limit, err)) {
            ctx.Defer([y, m, limit](ExpenseTracker& t) { t.SetOverallBudget(y, m, limit); return true; });
        } else {
            ctx.Status(StatusLevel::Error, err);
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    Health overallHealth = Classify(spentTotal, overall);
    char overlay[160];
    std::snprintf(overlay, sizeof(overlay), "%s / %s", MoneyUtil::Format(spentTotal, base).c_str(),
                  overall > 0 ? MoneyUtil::Format(overall, base).c_str() : "no limit");
    float fraction = overall > 0 ? static_cast<float>(static_cast<double>(spentTotal) / static_cast<double>(overall)) : 0.0f;
    Ui::ColoredProgressBar(fraction, HealthColor(overallHealth), ImVec2(u * 18, 0), overlay);
    ImGui::SameLine();
    HealthTag(overallHealth, spentTotal, overall, base);

    Money catSum = bm.GetCategoryBudgetSum(y, m);
    if (overall > 0 && catSum > overall) {
        ImGui::TextColored(tk.warning, "Category allocations (%s) exceed the overall budget by %s.",
                           MoneyUtil::Format(catSum, base).c_str(), MoneyUtil::Format(catSum - overall, base).c_str());
    }
    int excluded = tracker.CountUnconvertible(y, m);
    if (excluded > 0) {
        ImGui::TextColored(tk.warning, "%d transaction(s) have no exchange rate and are not counted.", excluded);
    }

    ImGui::Spacing();

    // ── Category workstation ──
    auto spending = tracker.GetCategoryBreakdown(y, m);
    auto budgets = bm.GetCategoryBudgetsForMonth(y, m);
    std::map<std::string, std::string> names;   // lower -> display
    for (auto& [cat, limit] : budgets) names.emplace(Utils::ToLower(cat), cat);
    for (auto& [cat, spent] : spending) names.emplace(Utils::ToLower(cat), cat);

    const float tableH = std::max(u * 10.0f, ImGui::GetContentRegionAvail().y - u * 13.0f);
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV |
                            ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("##CatBudgets", 6, flags, ImVec2(0, tableH))) {
        ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Budget allocated", ImGuiTableColumnFlags_WidthFixed, u * 9);
        ImGui::TableSetupColumn("Actual spent");
        ImGui::TableSetupColumn("Remaining");
        ImGui::TableSetupColumn("Progress", ImGuiTableColumnFlags_WidthFixed, u * 11);
        ImGui::TableSetupColumn("Status");
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        for (auto& [lower, display] : names) {
            Money limit = bm.GetCategoryBudget(y, m, display);
            auto sit = spending.find(display);
            Money spent = 0;
            if (sit != spending.end()) spent = sit->second;
            else {
                for (auto& [cat, v] : spending) {
                    if (Utils::ToLower(cat) == lower) spent = v;
                }
            }
            Health h = Classify(spent, limit);

            ImGui::TableNextRow();
            ImGui::PushID(lower.c_str());
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(display.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::BeginDisabled(ctx.readOnly);
            if (Ui::CommitDoubleInput("##limit", MoneyUtil::ToMajor(limit), committed, -FLT_MIN)) {
                Money newLimit;
                std::string err;
                if (Validation::ValidateAmount(committed, newLimit, err)) {
                    std::string cat = display;
                    ctx.Defer([y, m, cat, newLimit](ExpenseTracker& t) { t.SetCategoryBudget(y, m, cat, newLimit); return true; });
                } else {
                    ctx.Status(StatusLevel::Error, err);
                }
            }
            ImGui::EndDisabled();
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(MoneyUtil::Format(spent, base).c_str());
            ImGui::TableSetColumnIndex(3);
            if (limit > 0) {
                Money left = limit - spent;
                ImGui::TextColored(left >= 0 ? tk.text : tk.danger, "%s", MoneyUtil::Format(left, base).c_str());
            } else {
                ImGui::TextDisabled("-");
            }
            ImGui::TableSetColumnIndex(4);
            if (limit > 0) {
                double pct = static_cast<double>(spent) / static_cast<double>(limit) * 100.0;
                char pctText[32];
                std::snprintf(pctText, sizeof(pctText), "%.0f%%", pct);
                Ui::ColoredProgressBar(static_cast<float>(pct / 100.0), HealthColor(h), ImVec2(-FLT_MIN, 0), pctText);
            } else {
                ImGui::TextDisabled("set a budget");
            }
            ImGui::TableSetColumnIndex(5);
            HealthTag(h, spent, limit, base);
            ImGui::PopID();
        }
        if (names.empty()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("No spending or budgets this month yet.");
        }
        ImGui::EndTable();
    }

    // ── Add a category budget ──
    ImGui::BeginDisabled(ctx.readOnly);
    Ui::CategoryInput("##newcat", newCategory, tracker.GetCategories(), u * 12);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(u * 8);
    ImGui::InputDouble("##newlimit", &newLimit, 0.0, 0.0, "%.2f");
    ImGui::SameLine();
    if (ImGui::Button("Set category budget")) {
        Money limit;
        addError.clear();
        if (Validation::NormalizeText(newCategory, Validation::kMaxCategoryBytes).empty()) {
            addError = "Enter a category name.";
        } else if (!Validation::ValidateAmount(newLimit, limit, addError)) {
        } else if (limit == 0) {
            addError = "Enter a budget greater than zero.";
        } else {
            std::string cat = newCategory;
            ctx.Defer([y, m, cat, limit](ExpenseTracker& t) { t.SetCategoryBudget(y, m, cat, limit); return true; });
            newCategory.clear();
            newLimit = 0.0;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    Ui::MutedText("Set a budget to 0 to remove it.");
    Ui::ErrorText(addError);

    // ── Year overview ──
    ImGui::Spacing();
    ImGui::Text("%d overview", y);
    auto monthly = tracker.GetMonthlyTotals(y);
    if (ImGui::BeginTable("##YearOverview", 13, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("");
        for (int i = 0; i < 12; i++) ImGui::TableSetupColumn(Ui::kMonthShort[i]);
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        Ui::MutedText("Spent");
        for (int i = 0; i < 12; i++) {
            ImGui::TableSetColumnIndex(i + 1);
            Money spent = monthly[static_cast<size_t>(i)];
            Money limit = bm.GetOverallBudget(y, i + 1);
            char buf[48];
            Ui::CompactMoneyFormatter(MoneyUtil::ToMajor(spent), buf, sizeof(buf), const_cast<char*>(base.c_str()));
            ImGui::TextColored(limit > 0 ? HealthColor(Classify(spent, limit)) : tk.text, "%s", buf);
        }
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        Ui::MutedText("Budget");
        for (int i = 0; i < 12; i++) {
            ImGui::TableSetColumnIndex(i + 1);
            Money limit = bm.GetOverallBudget(y, i + 1);
            if (limit <= 0) {
                ImGui::TextDisabled("-");
                continue;
            }
            char buf[48];
            Ui::CompactMoneyFormatter(MoneyUtil::ToMajor(limit), buf, sizeof(buf), const_cast<char*>(base.c_str()));
            ImGui::TextUnformatted(buf);
        }
        ImGui::EndTable();
    }
}
