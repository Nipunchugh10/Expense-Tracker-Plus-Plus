#pragma once
#include <string>
#include "Expense.h"
#include "Validation.h"

struct FilterCriteria {
    std::string searchText;
    Date        dateFrom = {Validation::kMinYear, 1, 1};
    Date        dateTo   = {Validation::kMaxYear, 12, 31};
    std::string category;       // empty = all
    std::string currency;       // empty = all
    Money       amountMin = 0;
    Money       amountMax = MoneyUtil::kMaxAmount;
    int         type = -1;      // -1 = all, otherwise TransactionType value

    bool Matches(const Expense& e) const;
    bool operator==(const FilterCriteria& o) const;
    bool operator!=(const FilterCriteria& o) const { return !(*this == o); }
    void Reset();
    // Swaps inverted date/amount ranges. Returns true if anything was swapped.
    bool Normalize();
};
