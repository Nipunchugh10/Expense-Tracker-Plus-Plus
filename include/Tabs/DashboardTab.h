#pragma once
#include "AppContext.h"

class DashboardTab {
public:
    void Render(AppContext& ctx);

private:
    int  selectedYear = 0;    // initialised from today on first render
    int  selectedMonth = 0;   // 0 = all months
    bool openResetConfirm = false;
};
