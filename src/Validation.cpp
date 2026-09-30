#include "Validation.h"
#include "Expense.h"
#include "Utils.h"
#include <cmath>

namespace Validation {

int ClampYear(int year) {
    if (year < kMinYear) return kMinYear;
    if (year > kMaxYear) return kMaxYear;
    return year;
}

bool ValidateMoney(Money m, std::string& error) {
    if (m < 0) {
        error = "Amount cannot be negative (use the Income/Expense type instead of a sign).";
        return false;
    }
    if (m > MoneyUtil::kMaxAmount) {
        error = "Amount is too large (maximum is 100,000,000,000).";
        return false;
    }
    return true;
}

bool ValidateAmount(double major, Money& out, std::string& error) {
    if (!std::isfinite(major)) {
        error = "Amount must be a finite number.";
        return false;
    }
    if (major < 0.0) {
        error = "Amount cannot be negative (use the Income/Expense type instead of a sign).";
        return false;
    }
    if (major > MoneyUtil::ToMajor(MoneyUtil::kMaxAmount)) {
        error = "Amount is too large (maximum is 100,000,000,000).";
        return false;
    }
    out = MoneyUtil::FromMajor(major);
    return true;
}

std::string NormalizeText(const std::string& s, size_t maxBytes) {
    std::string clean = Utils::SanitizeUtf8(s);
    for (char& c : clean) {
        unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F) c = ' ';
    }
    return Utils::TruncateUtf8(Utils::Trim(clean), maxBytes);
}

bool NormalizeCurrency(const std::string& s, std::string& out) {
    std::string v = Utils::ToUpper(Utils::Trim(s));
    if (v.size() != 3) return false;
    for (char c : v) {
        if (c < 'A' || c > 'Z') return false;
    }
    out = v;
    return true;
}

std::string NormalizeCategory(const std::string& s) {
    std::string v = NormalizeText(s, kMaxCategoryBytes);
    // Collapse internal runs of spaces so "Food  Court" == "Food Court".
    std::string collapsed;
    for (char c : v) {
        if (c == ' ' && !collapsed.empty() && collapsed.back() == ' ') continue;
        collapsed += c;
    }
    return collapsed.empty() ? kFallbackCategory : collapsed;
}

bool NormalizeExpense(Expense& e, std::string& error) {
    std::string desc = NormalizeText(e.GetDescription(), kMaxDescriptionBytes);
    if (desc.empty()) {
        error = "Description is required.";
        return false;
    }
    if (!e.GetDate().IsValid()) {
        error = "Date is not a valid calendar date between 1900 and 2100.";
        return false;
    }
    if (!ValidateMoney(e.GetAmount(), error)) return false;

    std::string currency;
    if (!NormalizeCurrency(e.GetCurrency(), currency)) {
        error = "Currency must be a 3-letter code such as INR or USD.";
        return false;
    }
    e.SetDescription(desc);
    e.SetCurrency(currency);
    e.SetCategory(NormalizeCategory(e.GetCategory()));
    return true;
}

} // namespace Validation
