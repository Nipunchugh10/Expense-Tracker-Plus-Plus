#include "Tabs/SubscriptionsTab.h"
#include "AutoCategorizer.h"
#include "ExpenseTracker.h"
#include "ThemeManager.h"
#include "UiHelpers.h"
#include "Utils.h"
#include "Validation.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include <algorithm>
#include <vector>

namespace {

// "Renews every ..." presets shown when creating a subscription.
struct Period {
    const char* label;
    Frequency   frequency;
    int         days;   // for EveryNDays
};
const Period kPeriods[] = {
    {"Every day", Frequency::Daily, 1},
    {"Every 7 days (weekly)", Frequency::Weekly, 7},
    {"Every 14 days", Frequency::EveryNDays, 14},
    {"Every 28 days", Frequency::EveryNDays, 28},
    {"Every 30 days", Frequency::EveryNDays, 30},
    {"Same date every month", Frequency::Monthly, 0},
    {"Every 84 days (3-month plans)", Frequency::EveryNDays, 84},
    {"Every 90 days", Frequency::EveryNDays, 90},
    {"Every 180 days", Frequency::EveryNDays, 180},
    {"Every year (same date)", Frequency::Yearly, 0},
    {"Custom number of days...", Frequency::EveryNDays, -1},
};
constexpr int kPeriodCount = static_cast<int>(sizeof(kPeriods) / sizeof(kPeriods[0]));
constexpr int kCustomPeriod = kPeriodCount - 1;

std::string Countdown(long long days, const RecurringRule& r) {
    if (days <= 0) return "Renews today at " + r.TimeString();
    if (days == 1) return "1 day remaining until next renewal";
    return std::to_string(days) + " days remaining until next renewal";
}

} // namespace

// ── Form ──────────────────────────────────────────────────────────

void SubscriptionsTab::RuleForm::Reset(const Date& today, const std::string& cur) {
    *this = RuleForm();
    startDate = today;
    endDate = Utils::AddYears(today, 1);
    currency = cur;
}

void SubscriptionsTab::RuleForm::LoadFrom(const RecurringRule& r) {
    *this = RuleForm();
    description = r.GetDescription();
    amount = MoneyUtil::ToMajor(r.GetAmount());
    category = r.GetCategory();
    currency = r.GetCurrency();
    periodIdx = kCustomPeriod;
    customDays = std::max(1, r.GetIntervalDays());
    for (int i = 0; i < kCustomPeriod; i++) {
        const Period& p = kPeriods[i];
        bool match = p.frequency == r.GetFrequency() &&
                     (r.GetFrequency() != Frequency::EveryNDays || p.days == r.GetIntervalDays());
        if (match) {
            periodIdx = i;
            break;
        }
    }
    renewalMinutes = r.GetRenewalMinutes();
    autoChoice = r.IsAutoRecord() ? 1 : 0;
    startDate = r.GetStartDate();
    hasEndDate = r.GetEndDate() < Date{Validation::kMaxYear, 12, 31};
    endDate = hasEndDate ? r.GetEndDate() : Utils::AddYears(r.GetStartDate(), 1);
    active = r.IsActive();
    note = r.GetNote();
}

bool SubscriptionsTab::RuleForm::Build(RecurringRule& out) {
    error.clear();
    if (autoChoice < 0) {
        error = "Please choose whether renewals should be recorded in Expenses automatically.";
        return false;
    }
    Money m;
    if (!Validation::ValidateAmount(amount, m, error)) return false;
    RecurringRule r = out;   // keeps ID and bookkeeping when editing
    r.SetDescription(description);
    r.SetAmount(m);
    r.SetCategory(category);
    r.SetCurrency(currency);
    const Period& p = kPeriods[std::clamp(periodIdx, 0, kPeriodCount - 1)];
    r.SetFrequency(p.frequency);
    r.SetIntervalDays(p.days > 0 ? p.days : (p.days < 0 ? customDays : r.GetIntervalDays()));
    r.SetRenewalMinutes(renewalMinutes);
    r.SetAutoRecord(autoChoice == 1);
    r.SetStartDate(startDate);
    r.SetEndDate(hasEndDate ? endDate : Date{Validation::kMaxYear, 12, 31});
    r.SetActive(active);
    r.SetNote(note);
    if (!r.Normalize(error)) return false;
    out = r;
    return true;
}

// ── Tab ───────────────────────────────────────────────────────────

void SubscriptionsTab::Render(AppContext& ctx) {
    const ExpenseTracker& tracker = *ctx.tracker;
    ImGui::Spacing();
    ImGui::BeginDisabled(ctx.readOnly);
    if (ImGui::Button("+ Add Subscription")) {
        addForm.Reset(ctx.today, tracker.GetBaseCurrency());
        openAddModal = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Record due payments now")) {
        if (ctx.requestRecurringGeneration) ctx.requestRecurringGeneration();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    Ui::MutedText("Automatic subscriptions are recorded at their renewal time; the others are listed below for you.");
    ImGui::Spacing();

    RenderSummaryCards(ctx);
    ImGui::Spacing();
    RenderDuePayments(ctx);
    RenderRenewalCountdown(ctx);
    ImGui::Spacing();
    RenderRulesTable(ctx);

    RenderRuleModal(ctx, false);
    RenderRuleModal(ctx, true);
    RenderDeleteConfirm(ctx);
}

void SubscriptionsTab::RenderSummaryCards(AppContext& ctx) {
    const ExpenseTracker& tracker = *ctx.tracker;
    const ThemeTokens& tk = ThemeManager::Tokens();
    const std::string& base = tracker.GetBaseCurrency();

    int activeCount = 0, unconvertible = 0;
    Money monthly = 0;
    for (auto& r : tracker.GetRecurringRules()) {
        if (!r.IsActive()) continue;
        Date next;
        if (!r.NextOccurrenceOnOrAfter(ctx.today, next)) continue;   // ended
        activeCount++;
        Money v;
        if (tracker.GetCurrency().ConvertToBase(r.GetMonthlyEquivalent(), r.GetCurrency(), v)) monthly += v;
        else unconvertible++;
    }

    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float w = (ImGui::GetContentRegionAvail().x - gap * 2) / 3.0f;
    const float h = ImGui::GetFontSize() * 5.4f;   // room for the subtitle line
    Ui::KpiCard("##subCount", "Active subscriptions", std::to_string(activeCount), tk.accent, w, h);
    ImGui::SameLine();
    Ui::KpiCard("##subMonthly", "Monthly fixed burn rate", MoneyUtil::Format(monthly, base), tk.expense, w, h,
                "every period converted to a monthly amount");
    ImGui::SameLine();
    Ui::KpiCard("##subAnnual", "Annual projected fixed expenses", MoneyUtil::Format(monthly * 12, base), tk.warning, w, h);
    if (unconvertible > 0) {
        ImGui::TextColored(tk.warning, "%d subscription(s) use a currency without an exchange rate and are not included.",
                           unconvertible);
    }
}

void SubscriptionsTab::RenderDuePayments(AppContext& ctx) {
    const ExpenseTracker& tracker = *ctx.tracker;
    const ThemeTokens& tk = ThemeManager::Tokens();
    const int nowMin = Date::NowMinutes();

    struct Due { const RecurringRule* rule; std::vector<Date> dates; };
    std::vector<Due> due;
    for (auto& r : tracker.GetRecurringRules()) {
        auto dates = tracker.GetUnrecordedRenewals(r, ctx.today, nowMin, 50);
        if (!dates.empty()) due.push_back({&r, std::move(dates)});
    }
    if (due.empty()) return;

    ImVec4 bg = tk.danger;
    bg.w = 0.10f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
    float rowH = ImGui::GetFrameHeightWithSpacing();
    float h = rowH * (static_cast<float>(due.size()) + 1.4f) + ImGui::GetStyle().WindowPadding.y * 2;
    ImGui::BeginChild("##duePayments", ImVec2(0, std::min(h, ImGui::GetFontSize() * 12)), ImGuiChildFlags_Borders);
    ImGui::TextColored(tk.danger, "Payments to record (automatic recording is off for these)");
    for (auto& d : due) {
        const RecurringRule& r = *d.rule;
        const Date& oldest = d.dates.front();
        long long late = Utils::DaysBetween(oldest, ctx.today);
        ImGui::PushID(r.GetID());
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s - %s due %s %s", r.GetDescription().c_str(),
                    MoneyUtil::Format(r.GetAmount(), r.GetCurrency()).c_str(), oldest.ToString().c_str(),
                    r.TimeString().c_str());
        ImGui::SameLine();
        if (late > 0) ImGui::TextColored(tk.danger, "(overdue %d day%s)", static_cast<int>(late), late == 1 ? "" : "s");
        else ImGui::TextColored(tk.warning, "(due today)");
        if (d.dates.size() > 1) {
            ImGui::SameLine();
            Ui::MutedText("+%d more", static_cast<int>(d.dates.size()) - 1);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(ctx.readOnly);
        int id = r.GetID();
        Date when = oldest;
        if (ImGui::SmallButton("Record payment")) {
            ctx.Defer([id, when](ExpenseTracker& t) { return t.RecordSubscriptionPayment(id, when) != 0; });
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Skip")) {
            ctx.Defer([id, when](ExpenseTracker& t) { return t.SkipSubscriptionPayment(id, when); });
        }
        if (d.dates.size() > 1) {
            ImGui::SameLine();
            std::vector<Date> all = d.dates;
            if (ImGui::SmallButton("Skip all")) {
                ctx.Defer([id, all](ExpenseTracker& t) {
                    for (auto& date : all) {
                        if (!t.SkipSubscriptionPayment(id, date)) return false;
                    }
                    return true;
                });
            }
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

void SubscriptionsTab::RenderRenewalCountdown(AppContext& ctx) {
    const ExpenseTracker& tracker = *ctx.tracker;
    const ThemeTokens& tk = ThemeManager::Tokens();
    const int nowMin = Date::NowMinutes();

    struct Soon { long long days; Date date; const RecurringRule* rule; };
    std::vector<Soon> soon;
    for (auto& r : tracker.GetRecurringRules()) {
        Date next;
        if (!r.IsActive() || !r.NextRenewal(ctx.today, nowMin, next)) continue;
        long long days = Utils::DaysBetween(ctx.today, next);
        if (days <= 7) soon.push_back({days, next, &r});
    }
    if (soon.empty()) {
        Ui::MutedText("No renewals in the next 7 days.");
        return;
    }
    std::sort(soon.begin(), soon.end(), [](const Soon& a, const Soon& b) {
        if (a.days != b.days) return a.days < b.days;
        return a.rule->GetRenewalMinutes() < b.rule->GetRenewalMinutes();
    });

    ImVec4 bg = tk.warning;
    bg.w = 0.12f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
    float h = ImGui::GetTextLineHeightWithSpacing() * (static_cast<float>(soon.size()) + 1.4f) +
              ImGui::GetStyle().WindowPadding.y * 2;
    ImGui::BeginChild("##upcoming", ImVec2(0, std::min(h, ImGui::GetFontSize() * 11)), ImGuiChildFlags_Borders);
    ImGui::TextColored(tk.warning, "Renewing within 7 days");
    for (auto& s : soon) {
        const RecurringRule& r = *s.rule;
        ImGui::Bullet();
        ImGui::TextColored(tk.text, "%s", r.GetDescription().c_str());
        ImGui::SameLine();
        ImGui::TextColored(s.days <= 1 ? tk.danger : tk.warning, "- %s", Countdown(s.days, r).c_str());
        ImGui::SameLine();
        Ui::MutedText("(%s at %s, %s, %s)", s.date.ToString().c_str(), r.TimeString().c_str(),
                      MoneyUtil::Format(r.GetAmount(), r.GetCurrency()).c_str(),
                      r.IsAutoRecord() ? "recorded automatically" : "you record it");
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void SubscriptionsTab::RenderRulesTable(AppContext& ctx) {
    const ExpenseTracker& tracker = *ctx.tracker;
    const ThemeTokens& tk = ThemeManager::Tokens();
    const int nowMin = Date::NowMinutes();

    if (tracker.GetRecurringRules().empty()) {
        ImGui::TextDisabled("No subscriptions yet. Add rent, streaming services, mobile plans, insurance and other bills.");
        return;
    }

    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV |
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("##Rules", 10, flags, ImVec2(0, ImGui::GetContentRegionAvail().y))) return;
    ImGui::TableSetupColumn("Status");
    ImGui::TableSetupColumn("Service", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Amount");
    ImGui::TableSetupColumn("Renews");
    ImGui::TableSetupColumn("Time");
    ImGui::TableSetupColumn("Next renewal");
    ImGui::TableSetupColumn("Countdown");
    ImGui::TableSetupColumn("Recording");
    ImGui::TableSetupColumn("Category");
    ImGui::TableSetupColumn("Actions");
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();

    for (auto& r : tracker.GetRecurringRules()) {
        ImGui::TableNextRow();
        ImGui::PushID(r.GetID());
        Date next;
        bool hasNext = r.NextRenewal(ctx.today, nowMin, next);

        ImGui::TableSetColumnIndex(0);
        if (!hasNext) Ui::Badge("Ended", tk.muted);
        else if (r.IsActive()) Ui::Badge("Active", tk.success);
        else Ui::Badge("Paused", tk.warning);

        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(r.GetDescription().c_str());
        if (!r.GetNote().empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", r.GetNote().c_str());

        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(MoneyUtil::Format(r.GetAmount(), r.GetCurrency()).c_str());

        ImGui::TableSetColumnIndex(3);
        ImGui::TextUnformatted(r.CycleLabel().c_str());

        ImGui::TableSetColumnIndex(4);
        ImGui::TextUnformatted(r.TimeString().c_str());

        ImGui::TableSetColumnIndex(5);
        if (hasNext) ImGui::TextUnformatted(next.ToString().c_str());
        else ImGui::TextDisabled("-");

        ImGui::TableSetColumnIndex(6);
        if (hasNext && r.IsActive()) {
            long long days = Utils::DaysBetween(ctx.today, next);
            if (days == 0) ImGui::TextColored(tk.danger, "Today %s", r.TimeString().c_str());
            else if (days <= 7) ImGui::TextColored(days <= 1 ? tk.danger : tk.warning, "%d day%s left", static_cast<int>(days), days == 1 ? "" : "s");
            else Ui::MutedText("in %d days", static_cast<int>(days));
        } else {
            ImGui::TextDisabled("-");
        }

        ImGui::TableSetColumnIndex(7);
        if (r.IsAutoRecord()) ImGui::TextColored(tk.success, "Automatic");
        else Ui::MutedText("Manual");

        ImGui::TableSetColumnIndex(8);
        ImGui::TextUnformatted(r.GetCategory().c_str());

        ImGui::TableSetColumnIndex(9);
        ImGui::BeginDisabled(ctx.readOnly);
        int id = r.GetID();
        bool active = r.IsActive();
        if (ImGui::SmallButton(active ? "Pause" : "Resume")) {
            ctx.Defer([id, active](ExpenseTracker& t) { return t.SetRuleActive(id, !active); });
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Edit")) {
            editForm.LoadFrom(r);
            editRuleId = id;
            openEditModal = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) {
            deleteRuleId = id;
            deleteName = r.GetDescription();
            openDelete = true;
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void SubscriptionsTab::RenderFormFields(AppContext& ctx, RuleForm& form, bool isEdit) {
    const ExpenseTracker& tracker = *ctx.tracker;
    const ThemeTokens& tk = ThemeManager::Tokens();
    const float u = ImGui::GetFontSize();
    const float w = u * 18;

    ImGui::SetNextItemWidth(w);
    if (ImGui::InputTextWithHint("Service name", "e.g. Netflix, Airtel, Rent", &form.description) && ctx.categorizer &&
        (form.category.empty() || form.categoryAutoFilled)) {
        std::string s = ctx.categorizer->SuggestCategory(form.description);
        form.category = s;
        form.categoryAutoFilled = !s.empty();
    }
    ImGui::SetNextItemWidth(w);
    ImGui::InputDouble("Amount", &form.amount, 0.0, 0.0, "%.2f");
    Ui::CurrencyCombo("Currency", form.currency, tracker.GetCurrencyOptions(), u * 7);
    if (Ui::CategoryInput("Category", form.category, tracker.GetCategories(), w)) form.categoryAutoFilled = false;

    ImGui::SeparatorText("Renewal");
    ImGui::SetNextItemWidth(w);
    if (ImGui::BeginCombo("Renews every", kPeriods[std::clamp(form.periodIdx, 0, kPeriodCount - 1)].label)) {
        for (int i = 0; i < kPeriodCount; i++) {
            if (ImGui::Selectable(kPeriods[i].label, form.periodIdx == i)) form.periodIdx = i;
        }
        ImGui::EndCombo();
    }
    if (form.periodIdx == kCustomPeriod) {
        ImGui::SetNextItemWidth(u * 8);
        ImGui::InputInt("days between renewals", &form.customDays);
        form.customDays = std::clamp(form.customDays, 1, 3650);
    }
    const Period& p = kPeriods[std::clamp(form.periodIdx, 0, kPeriodCount - 1)];
    if (p.frequency == Frequency::Monthly) Ui::MutedText("Same date every month (the 31st becomes the last day in shorter months).");
    if (p.frequency == Frequency::Yearly) Ui::MutedText("Exactly one year apart, on the same date.");

    Ui::DateInput(isEdit ? "First renewal date" : "Next / first renewal date", form.startDate);
    Ui::TimeInput("Renewal time (24 h)", form.renewalMinutes);
    ImGui::Checkbox("Has an end date", &form.hasEndDate);
    if (form.hasEndDate) Ui::DateInput("End date", form.endDate);

    ImGui::SeparatorText("Record payments automatically?");
    if (form.autoChoice < 0) ImGui::TextColored(tk.warning, "Please choose one:");
    ImGui::RadioButton("Yes - add each renewal to the Expenses tab automatically", &form.autoChoice, 1);
    ImGui::RadioButton("No - I will record payments myself (they are listed as due)", &form.autoChoice, 0);
    Ui::MutedText("Either way you are reminded when 7 or fewer days remain before a renewal.");

    ImGui::Separator();
    ImGui::Checkbox("Active", &form.active);
    ImGui::SetNextItemWidth(w);
    ImGui::InputTextWithHint("Note", "optional", &form.note);
    if (isEdit) Ui::MutedText("Changes apply to renewals from now on; payments already recorded are unchanged.");
    Ui::ErrorText(form.error);
}

void SubscriptionsTab::RenderRuleModal(AppContext& ctx, bool isEdit) {
    const char* title = isEdit ? "Edit Subscription" : "Add Subscription";
    bool& open = isEdit ? openEditModal : openAddModal;
    RuleForm& form = isEdit ? editForm : addForm;
    if (open) {
        ImGui::OpenPopup(title);
        open = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    RenderFormFields(ctx, form, isEdit);
    ImGui::Separator();
    const float bw = ImGui::GetFontSize() * 7;
    if (ImGui::Button(isEdit ? "Save" : "Add", ImVec2(bw, 0)) || Ui::SubmitShortcutPressed()) {
        RecurringRule rule;
        if (isEdit) {
            if (const RecurringRule* existing = ctx.tracker->FindRule(editRuleId)) rule = *existing;
        }
        if (form.Build(rule)) {
            if (isEdit) {
                ctx.Defer([rule](ExpenseTracker& t) { return t.UpdateRecurringRule(rule); });
            } else {
                ctx.Defer([rule](ExpenseTracker& t) { return t.AddRecurringRule(rule) != 0; });
            }
            if (rule.IsAutoRecord() && ctx.requestRecurringGeneration) ctx.requestRecurringGeneration();
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void SubscriptionsTab::RenderDeleteConfirm(AppContext& ctx) {
    if (openDelete) {
        ImGui::OpenPopup("Delete Subscription?");
        openDelete = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Delete Subscription?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("Delete \"%s\"?", deleteName.c_str());
    Ui::MutedText("Payments already recorded stay in the Expenses tab. This cannot be undone.");
    ImGui::Spacing();
    const float bw = ImGui::GetFontSize() * 7;
    if (Ui::DangerButton("Delete", ImVec2(bw, 0))) {
        int id = deleteRuleId;
        ctx.Defer([id](ExpenseTracker& t) { return t.DeleteRecurringRule(id); });
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
