#pragma once
#include <string>
#include "Expense.h"

// A savings goal / sinking fund. Balances are tracked off-ledger: deposits
// allocate money that is already counted as net savings, so they never
// appear as income or expenses (G7).
class Goal {
public:
    int         id = 0;
    std::string title;                 // e.g. "Emergency Fund"
    Money       targetAmount = 0;
    Money       currentAmount = 0;
    Date        targetDate;
    std::string currency = kDefaultCurrency;
    std::string color = "#4C9AFF";     // hex colour tag

    double GetProgressPercentage() const;
    Money  GetRemainingAmount() const;
    bool   IsComplete() const { return targetAmount > 0 && currentAmount >= targetAmount; }

    // Number of monthly deposits left, counting the current month.
    // 0 when the target date has already passed.
    int MonthsRemaining(const Date& today) const;

    // Deposit per month needed to reach the target on time. When no months
    // remain (target date passed or this month) the whole remainder is due.
    Money GetRequiredMonthlyDeposit(const Date& today) const;

    bool Normalize(std::string& error);
};
