#include "Tabs/GoalsTab.h"
#include "ExpenseTracker.h"
#include "ThemeManager.h"
#include "UiHelpers.h"
#include "Utils.h"
#include "Validation.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {

ImVec4 ParseHex(const std::string& hex) {
    unsigned v = 0x4C9AFF;
    if (hex.size() == 7 && hex[0] == '#') v = static_cast<unsigned>(std::strtoul(hex.c_str() + 1, nullptr, 16));
    return ImVec4(((v >> 16) & 0xFF) / 255.0f, ((v >> 8) & 0xFF) / 255.0f, (v & 0xFF) / 255.0f, 1.0f);
}

std::string ToHex(const float c[3]) {
    auto channel = [](float f) { return static_cast<unsigned>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); };
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", channel(c[0]), channel(c[1]), channel(c[2]));
    return buf;
}

} // namespace

// ── Form ──────────────────────────────────────────────────────────

void GoalsTab::GoalForm::Reset(const Date& today, const std::string& cur) {
    *this = GoalForm();
    targetDate = Utils::AddMonths(today, 12);
    currency = cur;
}

void GoalsTab::GoalForm::LoadFrom(const Goal& g) {
    *this = GoalForm();
    title = g.title;
    target = MoneyUtil::ToMajor(g.targetAmount);
    current = MoneyUtil::ToMajor(g.currentAmount);
    targetDate = g.targetDate;
    currency = g.currency;
    ImVec4 c = ParseHex(g.color);
    color[0] = c.x;
    color[1] = c.y;
    color[2] = c.z;
}

bool GoalsTab::GoalForm::Build(Goal& out) {
    error.clear();
    Goal g = out;
    g.title = title;
    if (!Validation::ValidateAmount(target, g.targetAmount, error)) return false;
    if (!Validation::ValidateAmount(current, g.currentAmount, error)) return false;
    g.targetDate = targetDate;
    g.currency = currency;
    g.color = ToHex(color);
    if (!g.Normalize(error)) return false;
    out = g;
    return true;
}

// ── Tab ───────────────────────────────────────────────────────────

void GoalsTab::Render(AppContext& ctx) {
    const ExpenseTracker& tracker = *ctx.tracker;
    const float u = ImGui::GetFontSize();

    ImGui::Spacing();
    ImGui::BeginDisabled(ctx.readOnly);
    if (ImGui::Button("+ New Goal")) {
        addForm.Reset(ctx.today, tracker.GetBaseCurrency());
        openAdd = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    Ui::MutedText("Goal balances are allocations of your savings; deposits are not counted as expenses.");
    ImGui::Spacing();

    RenderSummary(ctx);
    ImGui::Spacing();

    const auto& goals = tracker.GetGoals();
    if (goals.empty()) {
        ImGui::TextDisabled("No goals yet. Create an emergency fund, a travel fund or a savings target for a purchase.");
    } else {
        ImGui::BeginChild("##GoalGrid", ImVec2(0, 0), ImGuiChildFlags_None);
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const float minW = u * 20;
        float avail = ImGui::GetContentRegionAvail().x;
        int columns = std::max(1, static_cast<int>((avail + gap) / (minW + gap)));
        float cardW = (avail - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns);
        for (size_t i = 0; i < goals.size(); i++) {
            if (i % static_cast<size_t>(columns) != 0) ImGui::SameLine();
            RenderCard(ctx, goals[i], cardW, u * 11.5f);
        }
        ImGui::EndChild();
    }

    RenderGoalModal(ctx, false);
    RenderGoalModal(ctx, true);
    RenderDepositModal(ctx);
    RenderDeleteConfirm(ctx);
}

void GoalsTab::RenderSummary(AppContext& ctx) {
    const ExpenseTracker& tracker = *ctx.tracker;
    const ThemeTokens& tk = ThemeManager::Tokens();
    const std::string& base = tracker.GetBaseCurrency();
    Money saved = 0, target = 0;
    int unconvertible = 0, completed = 0;
    for (auto& g : tracker.GetGoals()) {
        Money s, t;
        if (tracker.GetCurrency().ConvertToBase(g.currentAmount, g.currency, s) &&
            tracker.GetCurrency().ConvertToBase(g.targetAmount, g.currency, t)) {
            saved += s;
            target += t;
        } else {
            unconvertible++;
        }
        if (g.IsComplete()) completed++;
    }
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float w = (ImGui::GetContentRegionAvail().x - gap * 2) / 3.0f;
    const float h = ImGui::GetFontSize() * 4.6f;
    float progress = target > 0 ? static_cast<float>(static_cast<double>(saved) / static_cast<double>(target)) : 0.0f;
    Ui::KpiCard("##goalSaved", "Total saved toward goals", MoneyUtil::Format(saved, base), tk.success, w, h, nullptr,
                progress);
    ImGui::SameLine();
    Ui::KpiCard("##goalTarget", "Total of all targets", MoneyUtil::Format(target, base), tk.accent, w, h);
    ImGui::SameLine();
    std::string done = std::to_string(completed) + " of " + std::to_string(tracker.GetGoals().size());
    Ui::KpiCard("##goalDone", "Goals completed", done, tk.text, w, h);
    if (unconvertible > 0) {
        ImGui::TextColored(tk.warning, "%d goal(s) use a currency without an exchange rate and are not in the totals.",
                           unconvertible);
    }
}

void GoalsTab::RenderCard(AppContext& ctx, const Goal& g, float width, float height) {
    const ThemeTokens& tk = ThemeManager::Tokens();
    ImVec4 color = ParseHex(g.color);

    ImGui::PushID(g.id);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, tk.cardBg);
    ImGui::BeginChild("##card", ImVec2(width, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);

    // Colour tag strip
    ImVec2 p = ImGui::GetWindowPos();
    ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + 5.0f, p.y + ImGui::GetWindowHeight()),
                                              ImGui::GetColorU32(color), 6.0f, ImDrawFlags_RoundCornersLeft);

    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.2f);
    ImGui::TextUnformatted(g.title.c_str());
    ImGui::PopFont();

    double pct = g.GetProgressPercentage();
    char overlay[32];
    std::snprintf(overlay, sizeof(overlay), "%.1f%%", pct);
    Ui::ColoredProgressBar(static_cast<float>(pct / 100.0), g.IsComplete() ? tk.success : color, ImVec2(-FLT_MIN, 0), overlay);

    ImGui::Text("%s of %s", MoneyUtil::Format(g.currentAmount, g.currency).c_str(),
                MoneyUtil::Format(g.targetAmount, g.currency).c_str());

    const std::string when = std::string(Ui::kMonthShort[g.targetDate.month - 1]) + " " + std::to_string(g.targetDate.year);
    if (g.IsComplete()) {
        ImGui::TextColored(tk.success, "Goal reached!");
    } else {
        Money perMonth = g.GetRequiredMonthlyDeposit(ctx.today);
        int months = g.MonthsRemaining(ctx.today);
        if (months <= 0) {
            ImGui::TextColored(tk.danger, "Target date passed - %s still needed.",
                               MoneyUtil::Format(g.GetRemainingAmount(), g.currency).c_str());
        } else if (months == 1) {
            ImGui::TextColored(tk.warning, "Deposit %s this month to hit the target by %s.",
                               MoneyUtil::Format(perMonth, g.currency).c_str(), when.c_str());
        } else {
            ImGui::TextWrapped("Deposit %s/mo to hit the target by %s.", MoneyUtil::Format(perMonth, g.currency).c_str(),
                               when.c_str());
        }
    }
    Ui::MutedText("Target date %s", g.targetDate.ToString().c_str());

    ImGui::BeginDisabled(ctx.readOnly);
    if (ImGui::SmallButton("Deposit / Withdraw")) {
        depositGoalId = g.id;
        depositAmount = 0.0;
        depositMode = 0;
        depositError.clear();
        openDeposit = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Edit")) {
        editForm.LoadFrom(g);
        editGoalId = g.id;
        openEdit = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete")) {
        deleteGoalId = g.id;
        deleteTitle = g.title;
        openDelete = true;
    }
    ImGui::EndDisabled();

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopID();
}

void GoalsTab::RenderGoalModal(AppContext& ctx, bool isEdit) {
    const char* title = isEdit ? "Edit Goal" : "New Goal";
    bool& open = isEdit ? openEdit : openAdd;
    GoalForm& form = isEdit ? editForm : addForm;
    if (open) {
        ImGui::OpenPopup(title);
        open = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    const float u = ImGui::GetFontSize();
    ImGui::SetNextItemWidth(u * 18);
    ImGui::InputTextWithHint("Title", "e.g. Emergency Fund", &form.title);
    ImGui::SetNextItemWidth(u * 18);
    ImGui::InputDouble("Target amount", &form.target, 0.0, 0.0, "%.2f");
    ImGui::SetNextItemWidth(u * 18);
    ImGui::InputDouble("Already saved", &form.current, 0.0, 0.0, "%.2f");
    Ui::DateInput("Target date", form.targetDate);
    Ui::CurrencyCombo("Currency", form.currency, ctx.tracker->GetCurrencyOptions(), u * 7);
    ImGui::ColorEdit3("Colour", form.color, ImGuiColorEditFlags_NoInputs);
    Ui::ErrorText(form.error);
    ImGui::Separator();

    const float bw = u * 7;
    if (ImGui::Button(isEdit ? "Save" : "Create", ImVec2(bw, 0)) || Ui::SubmitShortcutPressed()) {
        Goal g;
        if (isEdit) {
            if (const Goal* existing = ctx.tracker->FindGoal(editGoalId)) g = *existing;
        }
        if (form.Build(g)) {
            if (isEdit) ctx.Defer([g](ExpenseTracker& t) { return t.UpdateGoal(g); });
            else ctx.Defer([g](ExpenseTracker& t) { return t.AddGoal(g) != 0; });
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void GoalsTab::RenderDepositModal(AppContext& ctx) {
    if (openDeposit) {
        ImGui::OpenPopup("Deposit / Withdraw");
        openDeposit = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Deposit / Withdraw", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    const Goal* g = ctx.tracker->FindGoal(depositGoalId);
    if (!g) {
        ImGui::TextDisabled("This goal no longer exists.");
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const float u = ImGui::GetFontSize();
    ImGui::Text("%s - balance %s", g->title.c_str(), MoneyUtil::Format(g->currentAmount, g->currency).c_str());
    ImGui::RadioButton("Deposit", &depositMode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Withdraw", &depositMode, 1);
    ImGui::SetNextItemWidth(u * 12);
    ImGui::InputDouble("Amount", &depositAmount, 0.0, 0.0, "%.2f");
    Ui::ErrorText(depositError);
    ImGui::Separator();
    const float bw = u * 7;
    if (ImGui::Button("Apply", ImVec2(bw, 0)) || Ui::SubmitShortcutPressed()) {
        Money amount;
        depositError.clear();
        if (!Validation::ValidateAmount(depositAmount, amount, depositError)) {
        } else if (amount == 0) {
            depositError = "Enter an amount greater than zero.";
        } else if (depositMode == 1 && amount > g->currentAmount) {
            depositError = "You cannot withdraw more than the current balance.";
        } else {
            int id = g->id;
            Money delta = depositMode == 1 ? -amount : amount;
            ctx.Defer([id, delta](ExpenseTracker& t) { return t.DepositToGoal(id, delta); });
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void GoalsTab::RenderDeleteConfirm(AppContext& ctx) {
    if (openDelete) {
        ImGui::OpenPopup("Delete Goal?");
        openDelete = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Delete Goal?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("Delete the goal \"%s\"?", deleteTitle.c_str());
    Ui::MutedText("This cannot be undone. Transactions are not affected.");
    const float bw = ImGui::GetFontSize() * 7;
    if (Ui::DangerButton("Delete", ImVec2(bw, 0))) {
        int id = deleteGoalId;
        ctx.Defer([id](ExpenseTracker& t) { return t.DeleteGoal(id); });
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
