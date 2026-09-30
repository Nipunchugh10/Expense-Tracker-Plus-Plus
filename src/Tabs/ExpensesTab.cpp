#include "Tabs/ExpensesTab.h"
#include "AutoCategorizer.h"
#include "CommandManager.h"
#include "ExpenseTracker.h"
#include "ThemeManager.h"
#include "UiHelpers.h"
#include "Utils.h"
#include "Validation.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include <algorithm>

// ── ExpenseForm ───────────────────────────────────────────────────

void ExpenseForm::Reset(const Date& today, const std::string& defaultCurrency) {
    *this = ExpenseForm();
    date = today;
    currency = defaultCurrency;
}

void ExpenseForm::LoadFrom(const Expense& e) {
    *this = ExpenseForm();
    type = e.GetType();
    description = e.GetDescription();
    amount = MoneyUtil::ToMajor(e.GetAmount());
    date = e.GetDate();
    category = e.GetCategory();
    currency = e.GetCurrency();
    recurringRuleId = e.GetRecurringRuleId();
}

bool ExpenseForm::Build(Expense& out) {
    error.clear();
    Money m;
    if (!Validation::ValidateAmount(amount, m, error)) return false;
    Expense e(0, description, m, date, category, currency, recurringRuleId, type);
    if (!Validation::NormalizeExpense(e, error)) return false;
    if (remember) {
        std::string kw = Validation::NormalizeText(rememberKeyword, Validation::kMaxKeywordBytes);
        if (kw.size() < Validation::kMinKeywordBytes) {
            error = "The keyword to remember must be at least 3 characters.";
            return false;
        }
    }
    out = e;
    return true;
}

// ── Tab ───────────────────────────────────────────────────────────

void ExpensesTab::Render(AppContext& ctx) {
    const ExpenseTracker& tracker = *ctx.tracker;
    if (!filterInitialized) {
        filterFrom = {ctx.today.year, ctx.today.month, 1};
        filterTo = ctx.today;
        filterInitialized = true;
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(ctx.readOnly);
    if (ImGui::Button("+ Add Transaction")) {
        addForm.Reset(ctx.today, tracker.GetBaseCurrency());
        openAdd = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    Ui::MutedText("Ctrl+Z / Ctrl+Y undo and redo transaction changes.");

    ImGui::Spacing();
    RenderFilterBar(ctx);
    ImGui::Separator();
    ImGui::Spacing();
    RenderTable(ctx);

    RenderAddPopup(ctx);
    RenderEditPopup(ctx);
    RenderDeleteConfirm(ctx);
}

void ExpensesTab::RenderFilterBar(AppContext& ctx) {
    const ExpenseTracker& tracker = *ctx.tracker;
    const float u = ImGui::GetFontSize();

    ImGui::SetNextItemWidth(u * 14);
    ImGui::InputTextWithHint("##Search", "Search description or category", &filter.searchText);

    ImGui::SameLine();
    const char* typeNames[] = {"All types", "Expenses", "Income", "Transfers", "Subscriptions"};
    int typeIdx = filter.type + 1;
    ImGui::SetNextItemWidth(u * 8);
    if (ImGui::Combo("##Type", &typeIdx, typeNames, 5)) filter.type = typeIdx - 1;

    ImGui::SameLine();
    ImGui::SetNextItemWidth(u * 10);
    if (ImGui::BeginCombo("##FilterCat", filter.category.empty() ? "All categories" : filter.category.c_str())) {
        if (ImGui::Selectable("All categories", filter.category.empty())) filter.category.clear();
        for (auto& cat : tracker.GetCategories()) {
            if (ImGui::Selectable(cat.c_str(), filter.category == cat)) filter.category = cat;
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(u * 7);
    if (ImGui::BeginCombo("##FilterCur", filter.currency.empty() ? "All currencies" : filter.currency.c_str())) {
        if (ImGui::Selectable("All currencies", filter.currency.empty())) filter.currency.clear();
        for (auto& cur : tracker.GetCurrencyOptions()) {
            if (ImGui::Selectable(cur.c_str(), filter.currency == cur)) filter.currency = cur;
        }
        ImGui::EndCombo();
    }

    ImGui::Checkbox("Date range", &useDateFilter);
    if (useDateFilter) {
        ImGui::SameLine();
        Ui::DateInput("from##filterFrom", filterFrom);
        ImGui::SameLine();
        Ui::DateInput("to##filterTo", filterTo);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(u * 6);
    ImGui::InputDouble("Min##amt", &filterMin, 0, 0, "%.2f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(u * 6);
    ImGui::InputDouble("Max (0 = none)##amt", &filterMax, 0, 0, "%.2f");
    ImGui::SameLine();
    if (ImGui::Button("Reset filters")) {
        filter.Reset();
        useDateFilter = false;
        filterMin = filterMax = 0.0;
    }
}

void ExpensesTab::RenderTable(AppContext& ctx) {
    const ExpenseTracker& tracker = *ctx.tracker;
    const ThemeTokens& tk = ThemeManager::Tokens();

    FilterCriteria f = filter;
    if (useDateFilter) {
        f.dateFrom = filterFrom;
        f.dateTo = filterTo;
    }
    std::string ignore;
    Money minM = 0, maxM = MoneyUtil::kMaxAmount;
    if (!Validation::ValidateAmount(filterMin, minM, ignore)) minM = 0;
    if (filterMax > 0.0 && !Validation::ValidateAmount(filterMax, maxM, ignore)) maxM = MoneyUtil::kMaxAmount;
    f.amountMin = minM;
    f.amountMax = maxM;
    if (f.Normalize()) Ui::MutedText("Note: the 'from' value was after the 'to' value, so the range was swapped.");

    // Filtering and footer totals are recomputed only when the data or the
    // filter changes, not every frame (large ledgers stay responsive).
    bool resort = false;
    if (tracker.GetRevision() != cachedRevision || f != cachedFilter) {
        rows = tracker.GetFilteredExpenses(f);
        cachedRevision = tracker.GetRevision();
        cachedFilter = f;
        resort = true;
        footerSpent = footerEarned = 0;
        footerExcluded = 0;
        for (auto* e : rows) {
            Money v;
            if (!tracker.ToBase(*e, v)) { if (!e->IsTransfer()) footerExcluded++; continue; }
            if (e->IsSpending()) footerSpent += v;
            else if (e->IsIncome()) footerEarned += v;
        }
    }

    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV |
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_SizingFixedFit;
    float tableHeight = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 1.3f;
    if (tableHeight < 100.0f) tableHeight = 100.0f;

    if (ImGui::BeginTable("##ExpenseTable", 8, flags, ImVec2(0, tableHeight))) {
        ImGui::TableSetupColumn("ID", 0, 0, 0);
        ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending, 0, 1);
        ImGui::TableSetupColumn("Type", 0, 0, 2);
        ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch, 0, 3);
        ImGui::TableSetupColumn("Amount", 0, 0, 4);
        ImGui::TableSetupColumn("Currency", 0, 0, 5);
        ImGui::TableSetupColumn("Category", 0, 0, 6);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_NoSort, 0, 7);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
            if (specs->SpecsCount > 0) {
                sortCol = static_cast<int>(specs->Specs[0].ColumnUserID);
                sortAsc = specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
            }
            specs->SpecsDirty = false;
        }
        if (resort || sortCol != sortedCol || sortAsc != sortedAsc) {
            SortRows();
            sortedCol = sortCol;
            sortedAsc = sortAsc;
        }

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                const Expense* e = rows[static_cast<size_t>(i)];
                ImGui::TableNextRow();
                ImGui::PushID(e->GetID());

                ImGui::TableSetColumnIndex(0);
                Ui::MutedText("%d", e->GetID());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(e->GetDate().ToString().c_str());
                ImGui::TableSetColumnIndex(2);
                const ImVec4& typeColor = e->IsIncome() ? tk.income : e->IsTransfer() ? tk.transfer
                                        : e->IsSubscription() ? tk.accent : tk.muted;
                if (e->IsIncome()) Ui::Badge("INCOME", typeColor);
                else if (e->IsTransfer()) Ui::Badge("TRANSFER", typeColor);
                else if (e->IsSubscription()) Ui::Badge("SUBSCRIPTION", typeColor);
                else ImGui::TextColored(typeColor, "Expense");
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(e->GetDescription().c_str());
                if (e->GetRecurringRuleId() != 0) {
                    ImGui::SameLine();
                    Ui::MutedText("(recurring)");
                }
                ImGui::TableSetColumnIndex(4);
                std::string amount = MoneyUtil::Format(e->GetAmount(), e->GetCurrency());
                if (e->IsIncome()) ImGui::TextColored(tk.income, "+%s", amount.c_str());
                else if (e->IsSpending()) ImGui::TextColored(tk.expense, "-%s", amount.c_str());
                else ImGui::TextColored(tk.transfer, "%s", amount.c_str());
                ImGui::TableSetColumnIndex(5);
                ImGui::TextUnformatted(e->GetCurrency().c_str());
                ImGui::TableSetColumnIndex(6);
                ImGui::TextUnformatted(e->GetCategory().c_str());
                ImGui::TableSetColumnIndex(7);
                ImGui::BeginDisabled(ctx.readOnly);
                if (ImGui::SmallButton("Edit")) {
                    editForm.LoadFrom(*e);
                    editID = e->GetID();
                    openEdit = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Delete")) {
                    deleteID = e->GetID();
                    deleteLabel = e->GetDescription() + " (" + MoneyUtil::Format(e->GetAmount(), e->GetCurrency()) + ")";
                    openDelete = true;
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

    // Footer: totals of the visible rows in the base currency (cached above).
    const Money spent = footerSpent, earned = footerEarned;
    const int excluded = footerExcluded;
    const std::string& base = tracker.GetBaseCurrency();
    ImGui::Text("%d transaction(s) shown", static_cast<int>(rows.size()));
    ImGui::SameLine();
    ImGui::TextColored(tk.expense, "  Expenses %s", MoneyUtil::Format(spent, base).c_str());
    ImGui::SameLine();
    ImGui::TextColored(tk.income, "  Income %s", MoneyUtil::Format(earned, base).c_str());
    if (excluded > 0) {
        ImGui::SameLine();
        ImGui::TextColored(tk.warning, "  (%d without an exchange rate not counted)", excluded);
    }
}

void ExpensesTab::SortRows() {
    auto idCmp = [](const Expense* a, const Expense* b) { return (a->GetID() > b->GetID()) - (a->GetID() < b->GetID()); };
    auto finish = [&](int r, const Expense* a, const Expense* b) {
        if (r == 0) r = idCmp(a, b);
        return sortAsc ? r < 0 : r > 0;
    };
    if (sortCol == 3 || sortCol == 6) {
        // Text columns: lower-case each key once instead of on every comparison.
        std::vector<std::pair<std::string, const Expense*>> keyed;
        keyed.reserve(rows.size());
        for (auto* e : rows) keyed.emplace_back(Utils::ToLower(sortCol == 3 ? e->GetDescription() : e->GetCategory()), e);
        std::sort(keyed.begin(), keyed.end(), [&](const auto& a, const auto& b) {
            return finish(a.first.compare(b.first), a.second, b.second);
        });
        for (size_t i = 0; i < keyed.size(); i++) rows[i] = keyed[i].second;
        return;
    }
    std::sort(rows.begin(), rows.end(), [&](const Expense* a, const Expense* b) {
        int r = 0;
        switch (sortCol) {
            case 0: r = idCmp(a, b); break;
            case 1: r = (a->GetDate().ToInt() > b->GetDate().ToInt()) - (a->GetDate().ToInt() < b->GetDate().ToInt()); break;
            case 2: r = static_cast<int>(a->GetType()) - static_cast<int>(b->GetType()); break;
            case 4: r = (a->GetAmount() > b->GetAmount()) - (a->GetAmount() < b->GetAmount()); break;
            case 5: r = a->GetCurrency().compare(b->GetCurrency()); break;
            default: break;
        }
        return finish(r, a, b);
    });
}

void ExpensesTab::RenderFormFields(AppContext& ctx, ExpenseForm& form, bool isEdit) {
    const ExpenseTracker& tracker = *ctx.tracker;
    const ThemeTokens& tk = ThemeManager::Tokens();
    const float u = ImGui::GetFontSize();
    const float w = u * 20;

    // Type toggle
    const TransactionType types[] = {TransactionType::Expense, TransactionType::Subscription, TransactionType::Income,
                                     TransactionType::Transfer};
    for (int i = 0; i < 4; i++) {
        if (i) ImGui::SameLine();
        bool selected = form.type == types[i];
        const ImVec4& color = types[i] == TransactionType::Income ? tk.income
                            : types[i] == TransactionType::Transfer ? tk.transfer
                            : types[i] == TransactionType::Subscription ? tk.accent : tk.expense;
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(color.x, color.y, color.z, 0.55f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x, color.y, color.z, 0.70f));
        }
        if (ImGui::Button(TransactionTypeLabel(types[i]), ImVec2(u * 6.5f, 0))) form.type = types[i];
        if (selected) ImGui::PopStyleColor(2);
    }
    if (form.type == TransactionType::Transfer) Ui::MutedText("Transfers are excluded from income, expenses and budgets.");
    if (isEdit && form.recurringRuleId != 0) Ui::MutedText("Generated by a subscription; edits apply to this occurrence only.");

    ImGui::SetNextItemWidth(w);
    if (ImGui::InputTextWithHint("Description", "e.g. Uber to airport", &form.description)) {
        if ((form.category.empty() || form.categoryAutoFilled) && ctx.categorizer) {
            std::string kw;
            std::string suggestion = ctx.categorizer->SuggestCategory(form.description, &kw);
            if (!suggestion.empty()) {
                form.category = suggestion;
                form.categoryAutoFilled = true;
                form.matchedKeyword = kw;
            } else if (form.categoryAutoFilled) {
                form.category.clear();
                form.categoryAutoFilled = false;
                form.matchedKeyword.clear();
            }
        }
        if (!form.remember) form.rememberKeyword = AutoCategorizer::SuggestKeyword(form.description);
    }

    ImGui::SetNextItemWidth(w);
    ImGui::InputDouble("Amount", &form.amount, 0.0, 0.0, "%.2f");

    Ui::DateInput("Date", form.date);

    if (Ui::CategoryInput("Category", form.category, tracker.GetCategories(), w)) {
        form.categoryAutoFilled = false;
        form.matchedKeyword.clear();
    }
    if (form.categoryAutoFilled && !form.matchedKeyword.empty()) {
        Ui::MutedText("Suggested because the description contains \"%s\".", form.matchedKeyword.c_str());
    }

    Ui::CurrencyCombo("Currency", form.currency, tracker.GetCurrencyOptions(), u * 7);

    ImGui::Checkbox("Remember", &form.remember);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(u * 9);
    ImGui::InputTextWithHint("##kw", "keyword", &form.rememberKeyword);
    ImGui::SameLine();
    Ui::MutedText("is always %s", form.category.empty() ? Validation::kFallbackCategory : form.category.c_str());

    Ui::ErrorText(form.error);
}

void ExpensesTab::ApplyRememberRule(AppContext& ctx, const ExpenseForm& form) {
    if (!form.remember || !ctx.categorizer) return;
    std::string err;
    if (ctx.categorizer->AddRule(form.rememberKeyword, form.category, err)) {
        if (ctx.saveCategorizer) ctx.saveCategorizer();
    } else {
        ctx.Status(StatusLevel::Warning, err);
    }
}

void ExpensesTab::RenderAddPopup(AppContext& ctx) {
    if (openAdd) {
        ImGui::OpenPopup("Add Transaction");
        openAdd = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Add Transaction", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        RenderFormFields(ctx, addForm, false);
        ImGui::Separator();
        const float bw = ImGui::GetFontSize() * 7;
        if (ImGui::Button("Add", ImVec2(bw, 0)) || Ui::SubmitShortcutPressed()) {
            Expense e;
            if (addForm.Build(e)) {
                ApplyRememberRule(ctx, addForm);
                ctx.DeferCommand([e](ExpenseTracker& t) { return std::make_unique<AddExpenseCommand>(t, e); });
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void ExpensesTab::RenderEditPopup(AppContext& ctx) {
    if (openEdit) {
        ImGui::OpenPopup("Edit Transaction");
        openEdit = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Edit Transaction", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Editing transaction #%d", editID);
        ImGui::Separator();
        RenderFormFields(ctx, editForm, true);
        ImGui::Separator();
        const float bw = ImGui::GetFontSize() * 7;
        if (ImGui::Button("Save", ImVec2(bw, 0)) || Ui::SubmitShortcutPressed()) {
            Expense e;
            if (editForm.Build(e)) {
                e.SetID(editID);
                ApplyRememberRule(ctx, editForm);
                ctx.DeferCommand([e](ExpenseTracker& t) { return std::make_unique<UpdateExpenseCommand>(t, e); });
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void ExpensesTab::RenderDeleteConfirm(AppContext& ctx) {
    if (openDelete) {
        ImGui::OpenPopup("Delete Transaction?");
        openDelete = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Delete Transaction?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete transaction #%d?", deleteID);
        ImGui::TextWrapped("%s", deleteLabel.c_str());
        Ui::MutedText("You can undo this with Ctrl+Z.");
        ImGui::Spacing();
        const float bw = ImGui::GetFontSize() * 7;
        if (Ui::DangerButton("Delete", ImVec2(bw, 0))) {
            int id = deleteID;
            ctx.DeferCommand([id](ExpenseTracker& t) { return std::make_unique<DeleteExpenseCommand>(t, id); });
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
