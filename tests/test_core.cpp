// Dates, money, validation, filters (Phase 0 groups B and C).
#include "TestFramework.h"
#include "TestHelpers.h"
#include "FilterCriteria.h"
#include "Utils.h"
#include "Validation.h"
#include <cmath>
#include <limits>

TEST_CASE("Date parsing is strict and never throws") {
    Date d;
    CHECK(Date::TryParse("2024-02-29", d));
    CHECK(d.year == 2024 && d.month == 2 && d.day == 29);
    CHECK(!Date::TryParse("2023-02-29", d));
    CHECK(!Date::TryParse("abcd-ef-gh", d));
    CHECK(!Date::TryParse("2025-13-01", d));
    CHECK(!Date::TryParse("2025-1-1", d));
    CHECK(!Date::TryParse("1899-12-31", d));
    CHECK(!Date::TryParse("", d));
    CHECK((Date{2100, 12, 31}.IsValid()));
    CHECK(!(Date{2025, 4, 31}.IsValid()));
}

TEST_CASE("Day numbers round-trip across the supported range") {
    for (Date d = {1900, 1, 1}; d.year < 2101; d = Utils::AddDays(d, 97)) {
        CHECK(Utils::FromDayNumber(Utils::ToDayNumber(d)) == d);
    }
    CHECK_EQ(Utils::DaysBetween(Date{2024, 2, 28}, Date{2024, 3, 1}), 2LL);
    CHECK(Utils::AddMonths(Date{2025, 1, 31}, 1) == (Date{2025, 2, 28}));
    CHECK(Utils::AddMonths(Date{2025, 12, 15}, 1) == (Date{2026, 1, 15}));
}

TEST_CASE("Money parsing is locale-independent and exact") {
    Money m = 0;
    CHECK(MoneyUtil::ParseDecimal("12345678.90", m) && m == 1234567890);
    CHECK(MoneyUtil::ParseDecimal("1,234.5", m) && m == 123450);
    CHECK(MoneyUtil::ParseDecimal("0.105", m) && m == 11);
    CHECK(MoneyUtil::ParseDecimal("-3", m) && m == -300);
    CHECK(!MoneyUtil::ParseDecimal("1.2.3", m));
    CHECK(!MoneyUtil::ParseDecimal("abc", m));
    CHECK(!MoneyUtil::ParseDecimal("", m));
    CHECK_EQ(MoneyUtil::ToDecimalString(1234567890), std::string("12345678.90"));
    CHECK_EQ(MoneyUtil::ToDecimalString(-5), std::string("-0.05"));
}

TEST_CASE("Money formatting uses currency symbols and grouping") {
    CHECK_EQ(MoneyUtil::Format(12345600, "INR"), std::string("\xE2\x82\xB9" "1,23,456.00"));
    CHECK_EQ(MoneyUtil::Format(123450, "USD"), std::string("$1,234.50"));
    CHECK_EQ(MoneyUtil::Format(-4500, "USD"), std::string("-$45.00"));
    CHECK_EQ(MoneyUtil::Format(100, "CHF"), std::string("CHF 1.00"));
}

TEST_CASE("Thirty 0.10 expenses exactly meet a 3.00 budget") {
    ExpenseTracker t;
    for (int i = 0; i < 30; i++) t.AddExpense(test::MakeExpense("Tea", 0.10, Date{2025, 6, 1}));
    t.SetOverallBudget(2025, 6, MoneyUtil::FromMajor(3.00));
    CHECK_EQ(t.GetMonthlyTotal(2025, 6), t.GetBudgetManager().GetOverallBudget(2025, 6));
}

TEST_CASE("Amount validation rejects NaN, infinity, negatives and huge values") {
    Money out;
    std::string err;
    CHECK(!Validation::ValidateAmount(std::numeric_limits<double>::quiet_NaN(), out, err));
    CHECK(!Validation::ValidateAmount(std::numeric_limits<double>::infinity(), out, err));
    CHECK(!Validation::ValidateAmount(-0.01, out, err));
    CHECK(!Validation::ValidateAmount(1e12, out, err));
    CHECK(Validation::ValidateAmount(19.999, out, err) && out == 2000);
}

TEST_CASE("Text normalization is UTF-8 safe") {
    std::string hindi = "\xE0\xA4\xA8\xE0\xA4\xAE\xE0\xA4\xB8\xE0\xA5\x8D\xE0\xA4\xA4\xE0\xA5\x87";   // नमस्ते
    CHECK_EQ(Validation::NormalizeText(hindi, 100), hindi);
    std::string cut = Validation::NormalizeText(hindi, 4);
    CHECK(Utils::IsValidUtf8(cut));
    CHECK_EQ(cut.size(), size_t(3));
    CHECK(Utils::IsValidUtf8(Validation::NormalizeText("bad\xFF\xFE bytes", 100)));
    CHECK_EQ(Validation::NormalizeText("  a\tb\n ", 100), std::string("a b"));
    CHECK_EQ(Validation::NormalizeText("one\r\ntwo\rthree", 100), std::string("one two three"));   // CRLF is one break
    std::string longText(1000, 'x');
    CHECK_EQ(Validation::NormalizeText(longText, Validation::kMaxDescriptionBytes), longText);
}

TEST_CASE("Currency and category normalization") {
    std::string c;
    CHECK(Validation::NormalizeCurrency(" usd ", c) && c == "USD");
    CHECK(!Validation::NormalizeCurrency("RUPEES", c));
    CHECK(!Validation::NormalizeCurrency("U1D", c));
    CHECK_EQ(Validation::NormalizeCategory("  Food   Court "), std::string("Food Court"));
    CHECK_EQ(Validation::NormalizeCategory("   "), std::string("General"));
}

TEST_CASE("Invalid expenses are rejected by the tracker") {
    ExpenseTracker t;
    CHECK(t.AddExpense(test::MakeExpense("", 5, Date{2025, 1, 1})) == 0);
    CHECK(t.AddExpense(test::MakeExpense("x", 5, Date{2025, 2, 30})) == 0);
    CHECK(t.AddExpense(test::MakeExpense("x", 5, Date{2025, 1, 1}, "Food", "RUPEES")) == 0);
    Expense neg = test::MakeExpense("x", 5, Date{2025, 1, 1});
    neg.SetAmount(-1);
    CHECK(t.AddExpense(neg) == 0);
    CHECK(t.GetExpenseCount() == 0);
    CHECK(!t.HasChanges());
}

TEST_CASE("Category variants collapse into one category") {
    ExpenseTracker t;
    t.AddExpense(test::MakeExpense("a", 1, Date{2025, 1, 1}, "Food"));
    t.AddExpense(test::MakeExpense("b", 2, Date{2025, 1, 2}, " food "));
    t.AddExpense(test::MakeExpense("c", 3, Date{2025, 1, 3}, "FOOD"));
    CHECK_EQ(t.GetCategories().size(), size_t(1));
    auto breakdown = t.GetCategoryBreakdown(2025, 1);
    CHECK_EQ(breakdown.size(), size_t(1));
    CHECK_EQ(breakdown["Food"], Money(600));
}

TEST_CASE("Filter criteria normalize inverted ranges") {
    FilterCriteria f;
    f.dateFrom = {2025, 5, 1};
    f.dateTo = {2025, 1, 1};
    f.amountMin = 500;
    f.amountMax = 100;
    CHECK(f.Normalize());
    CHECK(f.dateFrom == (Date{2025, 1, 1}));
    CHECK(f.amountMin == 100 && f.amountMax == 500);
    Expense e = test::MakeExpense("Lunch", 2, Date{2025, 3, 1}, "Food", "INR", TransactionType::Income);
    f.type = static_cast<int>(TransactionType::Expense);
    CHECK(!f.Matches(e));
    f.type = -1;
    CHECK(f.Matches(e));
}
