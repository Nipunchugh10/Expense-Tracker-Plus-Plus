#pragma once
#include <string>
#include "AppContext.h"
#include "Goal.h"

// Savings goals & sinking funds (Step 5).
class GoalsTab {
public:
    void Render(AppContext& ctx);

private:
    struct GoalForm {
        std::string title;
        double      target = 0.0;
        double      current = 0.0;
        Date        targetDate;
        std::string currency;
        float       color[3] = {0.30f, 0.60f, 1.00f};
        std::string error;

        void Reset(const Date& today, const std::string& currency);
        void LoadFrom(const Goal& g);
        bool Build(Goal& out);
    };

    void RenderSummary(AppContext& ctx);
    void RenderCard(AppContext& ctx, const Goal& g, float width, float height);
    void RenderGoalModal(AppContext& ctx, bool isEdit);
    void RenderDepositModal(AppContext& ctx);
    void RenderDeleteConfirm(AppContext& ctx);

    GoalForm addForm;
    GoalForm editForm;
    int  editGoalId = 0;
    bool openAdd = false;
    bool openEdit = false;

    int    depositGoalId = 0;
    double depositAmount = 0.0;
    int    depositMode = 0;   // 0 = deposit, 1 = withdraw
    std::string depositError;
    bool   openDeposit = false;

    int  deleteGoalId = 0;
    std::string deleteTitle;
    bool openDelete = false;
};
