#include "Expense.h"
#include "Utils.h"
#include "Validation.h"
#include <ctime>

// ── Date ──────────────────────────────────────────────────────────

int Date::ToInt() const { return year * 10000 + month * 100 + day; }

Date Date::FromInt(int v) {
    Date d;
    d.year  = v / 10000;
    d.month = (v / 100) % 100;
    d.day   = v % 100;
    return d;
}

bool Date::operator<(const Date& o)  const { return ToInt() < o.ToInt(); }
bool Date::operator<=(const Date& o) const { return ToInt() <= o.ToInt(); }
bool Date::operator==(const Date& o) const { return ToInt() == o.ToInt(); }
bool Date::operator!=(const Date& o) const { return ToInt() != o.ToInt(); }
bool Date::operator>=(const Date& o) const { return ToInt() >= o.ToInt(); }
bool Date::operator>(const Date& o)  const { return ToInt() > o.ToInt(); }

bool Date::IsValid() const {
    return year >= Validation::kMinYear && year <= Validation::kMaxYear &&
           month >= 1 && month <= 12 &&
           day >= 1 && day <= Utils::DaysInMonth(year, month);
}

static std::string Pad(int v, size_t width) {
    std::string s = std::to_string(v < 0 ? -v : v);
    while (s.size() < width) s = "0" + s;
    return (v < 0 ? "-" : "") + s;
}

std::string Date::ToString() const {
    return Pad(year, 4) + "-" + Pad(month, 2) + "-" + Pad(day, 2);
}

bool Date::TryParse(const std::string& s, Date& out) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    auto digits = [&](size_t from, size_t count, int& value) {
        value = 0;
        for (size_t i = from; i < from + count; i++) {
            if (s[i] < '0' || s[i] > '9') return false;
            value = value * 10 + (s[i] - '0');
        }
        return true;
    };
    Date d;
    if (!digits(0, 4, d.year) || !digits(5, 2, d.month) || !digits(8, 2, d.day)) return false;
    if (!d.IsValid()) return false;
    out = d;
    return true;
}

int Date::NowMinutes() {
    std::time_t t = std::time(nullptr);
    std::tm* tm = std::localtime(&t);
    return tm ? tm->tm_hour * 60 + tm->tm_min : 0;
}

Date Date::Today() {
    Date d;
    std::time_t t = std::time(nullptr);
    std::tm* tm = std::localtime(&t);
    if (tm) {
        d.year  = tm->tm_year + 1900;
        d.month = tm->tm_mon + 1;
        d.day   = tm->tm_mday;
    }
    return d;
}

// ── TransactionType ───────────────────────────────────────────────

const char* TransactionTypeToString(TransactionType t) {
    switch (t) {
        case TransactionType::Expense:  return "expense";
        case TransactionType::Income:   return "income";
        case TransactionType::Transfer: return "transfer";
        case TransactionType::Subscription: return "subscription";
        case TransactionType::Savings:  return "savings";
    }
    return "expense";
}

const char* TransactionTypeLabel(TransactionType t) {
    switch (t) {
        case TransactionType::Expense:  return "Expense";
        case TransactionType::Income:   return "Income";
        case TransactionType::Transfer: return "Transfer";
        case TransactionType::Subscription: return "Subscription";
        case TransactionType::Savings:  return "Savings";
    }
    return "Expense";
}

bool TransactionTypeFromString(const std::string& s, TransactionType& out) {
    std::string v = Utils::ToLower(Utils::Trim(s));
    if (v == "expense")  { out = TransactionType::Expense;  return true; }
    if (v == "income")   { out = TransactionType::Income;   return true; }
    if (v == "transfer") { out = TransactionType::Transfer; return true; }
    if (v == "subscription") { out = TransactionType::Subscription; return true; }
    if (v == "savings")  { out = TransactionType::Savings;  return true; }
    return false;
}

// ── Expense ───────────────────────────────────────────────────────

Expense::Expense(int id_, const std::string& description_, Money amount_,
                 const Date& date_, const std::string& category_,
                 const std::string& currency_, int recurringRuleId_, TransactionType type_)
    : id(id_), description(description_), amount(amount_), date(date_),
      category(category_), currency(currency_), recurringRuleId(recurringRuleId_), type(type_)
{
}
