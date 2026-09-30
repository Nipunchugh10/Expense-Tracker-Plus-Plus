#pragma once
#include "AppContext.h"

class AnalyticsTab {
public:
    void Render(AppContext& ctx);

private:
    void RenderForecast(AppContext& ctx);
    void RenderBreakdowns(AppContext& ctx);
    void RenderTrendAndTop(AppContext& ctx);

    int selectedYear = 0;
    int forecastMonth = 0;
};
