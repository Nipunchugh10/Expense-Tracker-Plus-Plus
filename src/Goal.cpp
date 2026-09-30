#include "Goal.h"
#include "Validation.h"
#include "Utils.h"

double Goal::GetProgressPercentage() const {
    if (targetAmount <= 0) return 0.0;
    return static_cast<double>(currentAmount) / static_cast<double>(targetAmount) * 100.0;
}

Money Goal::GetRemainingAmount() const {
    return targetAmount > currentAmount ? targetAmount - currentAmount : 0;
}

int Goal::MonthsRemaining(const Date& today) const {
    if (targetDate < today) return 0;
    return (targetDate.year - today.year) * 12 + (targetDate.month - today.month) + 1;
}

Money Goal::GetRequiredMonthlyDeposit(const Date& today) const {
    Money remaining = GetRemainingAmount();
    if (remaining == 0) return 0;
    int months = MonthsRemaining(today);
    if (months <= 1) return remaining;
    return (remaining + months - 1) / months;   // round up so the target is reached
}

static bool IsHexColor(const std::string& s) {
    if (s.size() != 7 || s[0] != '#') return false;
    for (size_t i = 1; i < 7; i++) {
        char c = s[i];
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex) return false;
    }
    return true;
}

bool Goal::Normalize(std::string& error) {
    title = Validation::NormalizeText(title, Validation::kMaxTitleBytes);
    if (title.empty()) {
        error = "Goal title is required.";
        return false;
    }
    if (!Validation::ValidateMoney(targetAmount, error)) return false;
    if (targetAmount == 0) {
        error = "Target amount must be greater than zero.";
        return false;
    }
    if (!Validation::ValidateMoney(currentAmount, error)) return false;
    if (!targetDate.IsValid()) {
        error = "Target date is not a valid date.";
        return false;
    }
    std::string cur;
    if (!Validation::NormalizeCurrency(currency, cur)) {
        error = "Currency must be a 3-letter code such as INR or USD.";
        return false;
    }
    currency = cur;
    if (!IsHexColor(color)) color = "#4C9AFF";
    return true;
}
