#pragma once
#include <cstdint>
#include <string>
#include <vector>

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

    // The character that separates whole units from decimals: "12.50" (Dot) or "12,50" (Comma).
    enum class DecimalMark { Dot, Comma };

    // Strict amount parser. Accepts an optional sign, digits, digit grouping and any number of
    // decimals (rounded to 2). Grouping marks are the other one of '.'/',' plus apostrophes and
    // (non-breaking) spaces, and must form real groups: "1,234,567" and the Indian "12,34,567".
    // So with DecimalMark::Dot, "12,50" is rejected instead of being read as 1250.
    bool ParseAmount(const std::string& text, DecimalMark mark, Money& out);

    // ParseAmount with DecimalMark::Dot.
    bool ParseDecimal(const std::string& text, Money& out);

    // Guesses the decimal mark used by a column of amounts ("1.234,56" and "12,5" mean Comma;
    // "1,234.56" and "12.5" mean Dot). Returns `fallback` when the samples do not tell.
    DecimalMark DetectDecimalMark(const std::vector<std::string>& samples, DecimalMark fallback);

    // Display formatting with currency symbol and digit grouping
    // (Indian lakh/crore grouping for INR), e.g. "₹1,23,456.00", "$1,234.50".
    std::string Format(Money m, const std::string& currency);
    std::string CurrencySymbol(const std::string& code);   // "" when none is known

    // Scales an amount by a floating factor (currency conversion), rounded.
    Money Scale(Money m, double factor);
}
