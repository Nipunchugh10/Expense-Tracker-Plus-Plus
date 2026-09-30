#pragma once
#include <string>
#include "AppContext.h"

// Monthly category budget workstation (Step 3). Reads budgets from the
// tracker every frame; no cached copies that can go stale (P0-E1).
class BudgetTab {
public:
    void Render(AppContext& ctx);

private:
    int selectedYear = 0;
    int selectedMonth = 0;
    std::string newCategory;
    double newLimit = 0.0;
    std::string addError;
};
