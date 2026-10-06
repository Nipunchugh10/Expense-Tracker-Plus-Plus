#pragma once
#include <string>
#include <vector>
#include "AppContext.h"
#include "Expense.h"
#include "FilterCriteria.h"

class ExpenseTracker;

// Add/Edit form state (std::string buffers: no fixed-size truncation, P0-B3).
struct ExpenseForm {
    TransactionType type = TransactionType::Expense;
    std::string description;
    double      amount = 0.0;
    Date        date;
    std::string category;
    std::string currency;
    int         recurringRuleId = 0;

    bool        categoryAutoFilled = false;
    std::string matchedKeyword;
    bool        remember = false;
    std::string rememberKeyword;
    std::string error;

    void Reset(const Date& today, const std::string& defaultCurrency);
    void LoadFrom(const Expense& e);
    bool Build(Expense& out);   // validates with the shared validators
};

class ExpensesTab {
public:
    void Render(AppContext& ctx);

private:
    void RenderFilterBar(AppContext& ctx);
    void RenderTable(AppContext& ctx);
    void RenderFormFields(AppContext& ctx, ExpenseForm& form, bool isEdit);
    void RenderAddPopup(AppContext& ctx);
    void RenderEditPopup(AppContext& ctx);
    void RenderDeleteConfirm(AppContext& ctx);
    void ApplyRememberRule(AppContext& ctx, const ExpenseForm& form);
    void SortRows();

    FilterCriteria filter;
    bool   useDateFilter = false;
    Date   filterFrom;
    Date   filterTo;
    double filterMin = 0.0;
    double filterMax = 0.0;   // 0 = no maximum
    bool   filterInitialized = false;

    ExpenseForm addForm;
    ExpenseForm editForm;
    int  editID = 0;
    bool openAdd = false;
    bool openEdit = false;

    int  deleteID = 0;
    std::string deleteLabel;
    bool openDelete = false;

    int  sortCol = 1;
    bool sortAsc = false;

    // Cached filtered/sorted rows: rebuilt only when the ledger revision, the
    // filter or the sort order changes.
    std::vector<const Expense*> rows;
    unsigned long long cachedRevision = ~0ULL;
    FilterCriteria cachedFilter;
    int   sortedCol = -1;
    bool  sortedAsc = false;
    Money footerSpent = 0;
    Money footerEarned = 0;
    Money footerSaved = 0;
    int   footerExcluded = 0;
};
