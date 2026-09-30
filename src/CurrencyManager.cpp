#include "CurrencyManager.h"
#include <cmath>

CurrencyManager::CurrencyManager() {
    ResetToDefaults();
}

void CurrencyManager::ResetToDefaults() {
    // Approximate manual defaults; the app is offline so users edit them.
    rates = {
        {"USD", 1.0},
        {"EUR", 1.08},
        {"GBP", 1.27},
        {"INR", 0.012},
        {"JPY", 0.0067},
        {"CAD", 0.73},
        {"AUD", 0.66},
    };
    baseCurrency = kDefaultCurrency;
    asOf.clear();
    source.clear();
}

static bool ValidRate(double v) {
    return std::isfinite(v) && v > 0.0 && v < 1e12;
}

bool CurrencyManager::SetBaseCurrency(const std::string& code) {
    if (!HasRate(code)) return false;
    baseCurrency = code;
    return true;
}

bool CurrencyManager::HasRate(const std::string& code) const {
    return rates.count(code) > 0;
}

std::optional<double> CurrencyManager::GetPivotRate(const std::string& code) const {
    auto it = rates.find(code);
    if (it == rates.end()) return std::nullopt;
    return it->second;
}

bool CurrencyManager::SetPivotRate(const std::string& code, double valueInPivot) {
    if (!ValidRate(valueInPivot)) return false;
    if (code == kPivot && valueInPivot != 1.0) return false;
    rates[code] = valueInPivot;
    return true;
}

bool CurrencyManager::RemoveRate(const std::string& code) {
    if (code == kPivot || code == baseCurrency) return false;
    return rates.erase(code) > 0;
}

std::optional<double> CurrencyManager::Factor(const std::string& from, const std::string& to) const {
    if (from == to) return 1.0;
    auto a = GetPivotRate(from);
    auto b = GetPivotRate(to);
    if (!a || !b) return std::nullopt;
    return *a / *b;
}

std::optional<double> CurrencyManager::GetRateInBase(const std::string& code) const {
    return Factor(code, baseCurrency);
}

bool CurrencyManager::SetRateInBase(const std::string& code, double valueInBase) {
    if (!ValidRate(valueInBase) || code == baseCurrency) return false;
    auto baseRate = GetPivotRate(baseCurrency);
    if (!baseRate) return false;

    if (code != kPivot) {
        rates[code] = valueInBase * *baseRate;
        return true;
    }
    // Editing the pivot's value in base redefines the base currency's own
    // pivot rate; keep every other currency's value in base unchanged.
    std::map<std::string, double> inBase;
    for (auto& [c, r] : rates) inBase[c] = r / *baseRate;
    inBase[kPivot] = valueInBase;
    double baseInPivot = 1.0 / valueInBase;
    for (auto& [c, v] : inBase) rates[c] = v * baseInPivot;
    rates[kPivot] = 1.0;
    rates[baseCurrency] = baseInPivot;
    return true;
}

bool CurrencyManager::Convert(Money amount, const std::string& from, const std::string& to, Money& out) const {
    auto f = Factor(from, to);
    if (!f) return false;
    out = (from == to) ? amount : MoneyUtil::Scale(amount, *f);
    return true;
}

bool CurrencyManager::ConvertToBase(Money amount, const std::string& from, Money& out) const {
    return Convert(amount, from, baseCurrency, out);
}

void CurrencyManager::Assign(const std::string& base, const std::map<std::string, double>& pivotRates,
                             const std::string& asOfDate) {
    rates.clear();
    for (auto& [code, v] : pivotRates) {
        if (ValidRate(v)) rates[code] = v;
    }
    rates[kPivot] = 1.0;
    baseCurrency = HasRate(base) ? base : (HasRate(kDefaultCurrency) ? std::string(kDefaultCurrency)
                                                                      : std::string(kPivot));
    asOf = asOfDate;
}
