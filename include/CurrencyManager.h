#pragma once
#include <map>
#include <optional>
#include <string>
#include "Money.h"

// Exchange-rate registry (Step 2). Rates are stored against a fixed pivot
// currency (USD): rate[c] = value of one unit of c in USD. The base currency
// is only a display/aggregation choice, so changing it never requires
// rebasing stored rates (G2). Owned by ExpenseTracker, not a singleton (G3).
class CurrencyManager {
public:
    static constexpr const char* kPivot = "USD";

    CurrencyManager();

    void ResetToDefaults();

    const std::string& GetBaseCurrency() const { return baseCurrency; }
    bool SetBaseCurrency(const std::string& code);   // requires a known rate

    bool HasRate(const std::string& code) const;
    std::optional<double> GetPivotRate(const std::string& code) const;
    bool SetPivotRate(const std::string& code, double valueInPivot);   // > 0 and finite
    bool RemoveRate(const std::string& code);                          // pivot and base are protected

    // "1 code = X base" view used by the rate editor.
    std::optional<double> GetRateInBase(const std::string& code) const;
    bool SetRateInBase(const std::string& code, double valueInBase);

    std::optional<double> Factor(const std::string& from, const std::string& to) const;
    bool Convert(Money amount, const std::string& from, const std::string& to, Money& out) const;
    bool ConvertToBase(Money amount, const std::string& from, Money& out) const;

    const std::map<std::string, double>& GetAllRates() const { return rates; }
    std::string GetAsOf() const { return asOf; }
    void SetAsOf(const std::string& s) { asOf = s; }
    // "" = built-in defaults, "manual" = edited by the user, otherwise the sync provider.
    const std::string& GetSource() const { return source; }
    void SetSource(const std::string& s) { source = s; }

    // Used by the loader: replaces all rates at once (invalid entries dropped).
    void Assign(const std::string& base, const std::map<std::string, double>& pivotRates,
                const std::string& asOfDate);

private:
    std::string baseCurrency;
    std::map<std::string, double> rates;   // code -> value in pivot (USD)
    std::string asOf;                      // "" = built-in approximate defaults
    std::string source;
};
