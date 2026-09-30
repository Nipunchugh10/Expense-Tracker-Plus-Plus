#pragma once
#include <string>
#include "AppContext.h"
#include "RecurringRule.h"

class ExpenseTracker;

class SubscriptionsTab {
public:
    void Render(AppContext& ctx);

private:
    struct RuleForm {
        std::string description;
        double      amount = 0.0;
        std::string category;
        std::string currency;
        int         periodIdx = 5;            // index into the "Renews every" presets (5 = same date monthly)
        int         customDays = 30;
        int         renewalMinutes = 9 * 60;  // 09:00
        int         autoChoice = -1;          // -1 = not chosen yet, 0 = record myself, 1 = automatic
        Date        startDate;
        bool        hasEndDate = false;
        Date        endDate;
        bool        active = true;
        std::string note;
        std::string error;
        bool        categoryAutoFilled = false;

        void Reset(const Date& today, const std::string& currency);
        void LoadFrom(const RecurringRule& r);
        bool Build(RecurringRule& out);
    };

    void RenderSummaryCards(AppContext& ctx);
    void RenderDuePayments(AppContext& ctx);
    void RenderRenewalCountdown(AppContext& ctx);
    void RenderRulesTable(AppContext& ctx);
    void RenderRuleModal(AppContext& ctx, bool isEdit);
    void RenderDeleteConfirm(AppContext& ctx);
    void RenderFormFields(AppContext& ctx, RuleForm& form, bool isEdit);

    RuleForm addForm;
    RuleForm editForm;
    int  editRuleId = 0;
    bool openAddModal = false;
    bool openEditModal = false;
    int  deleteRuleId = 0;
    std::string deleteName;
    bool openDelete = false;
};
