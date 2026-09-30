#pragma once
#include <cstdint>
#include <string>

// Monetary amounts are stored as signed 64-bit integers in hundredths
// ("minor units") so sums and budget comparisons are exact.
using Money = std::int64_t;

// The single application-wide default currency (P0-B5).
inline constexpr const char kDefaultCurrency[] = "INR";

namespace MoneyUtil {
    // Largest accepted amount: 100 billion major units.
    constexpr Money kMaxAmount = 100000000000LL * 100;

    Money  FromMajor(double major);            // rounds half away from zero
    double ToMajor(Money m);

    // Locale-independent plain decimal, e.g. "-1234.50" (used by CSV and reports).
    std::string ToDecimalString(Money m);

    // Locale-independent parser. Accepts an optional sign, digits, optional
    // grouping commas/spaces and up to any number of decimals (rounded to 2).
    bool ParseDecimal(const std::string& text, Money& out);

    // Display formatting with currency symbol and digit grouping
    // (Indian lakh/crore grouping for INR), e.g. "₹1,23,456.00", "$1,234.50".
    std::string Format(Money m, const std::string& currency);
    std::string CurrencySymbol(const std::string& code);   // "" when none is known

    // Scales an amount by a floating factor (currency conversion), rounded.
    Money Scale(Money m, double factor);
}
