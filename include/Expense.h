#pragma once
#include <string>
#include "Money.h"

struct Date {
    int year  = 2000;
    int month = 1;
    int day   = 1;

    bool operator<(const Date& o) const;
    bool operator<=(const Date& o) const;
    bool operator==(const Date& o) const;
    bool operator!=(const Date& o) const;
    bool operator>=(const Date& o) const;
    bool operator>(const Date& o) const;

    bool IsValid() const;                                  // year 1900-2100, real calendar day
    std::string ToString() const;                          // YYYY-MM-DD
    static bool TryParse(const std::string& s, Date& out); // strict YYYY-MM-DD, never throws
    static Date Today();
    static int  NowMinutes();                              // local time of day, 0..1439
    int ToInt() const;                                     // YYYYMMDD
    static Date FromInt(int v);
};

// Subscription = a payment for a recurring bill. It is spending: every
// "expense" total (budgets, cash flow, forecast, reports) includes it.
// Savings: money set aside (shown as an outflow, but not spending, so it never touches budgets).
enum class TransactionType { Expense = 0, Income = 1, Transfer = 2, Subscription = 3, Savings = 4 };
constexpr int kTransactionTypeCount = 5;

const char* TransactionTypeToString(TransactionType t);   // "expense" | "income" | "transfer" | "subscription" | "savings"
const char* TransactionTypeLabel(TransactionType t);      // "Expense" | "Income" | "Transfer" | "Subscription"
bool TransactionTypeFromString(const std::string& s, TransactionType& out);

class Expense {
public:
    Expense() = default;
    Expense(int id, const std::string& description, Money amount,
            const Date& date, const std::string& category,
            const std::string& currency, int recurringRuleId = 0,
            TransactionType type = TransactionType::Expense);

    int             GetID()              const { return id; }
    const std::string& GetDescription()  const { return description; }
    Money           GetAmount()          const { return amount; }
    const Date&     GetDate()            const { return date; }
    int             GetMonth()           const { return date.month; }
    int             GetYear()            const { return date.year; }
    const std::string& GetCategory()     const { return category; }
    const std::string& GetCurrency()     const { return currency; }
    int             GetRecurringRuleId() const { return recurringRuleId; }
    TransactionType GetType()            const { return type; }
    bool            IsIncome()           const { return type == TransactionType::Income; }
    bool            IsExpense()          const { return type == TransactionType::Expense; }
    bool            IsTransfer()         const { return type == TransactionType::Transfer; }
    bool            IsSubscription()     const { return type == TransactionType::Subscription; }
    bool            IsSavings()          const { return type == TransactionType::Savings; }
    // Money going out that counts in expense totals, budgets and charts. Transfers count too
    // (they are outgoing payments to another account or person); savings do not.
    bool            IsSpending()         const { return IsExpense() || IsSubscription() || IsTransfer(); }

    void SetID(int v)                           { id = v; }
    void SetDescription(const std::string& d)   { description = d; }
    void SetAmount(Money a)                     { amount = a; }
    void SetDate(const Date& d)                 { date = d; }
    void SetCategory(const std::string& c)      { category = c; }
    void SetCurrency(const std::string& c)      { currency = c; }
    void SetRecurringRuleId(int r)              { recurringRuleId = r; }
    void SetType(TransactionType t)             { type = t; }

private:
    int             id = 0;
    std::string     description;
    Money           amount = 0;
    Date            date;
    std::string     category;
    std::string     currency = kDefaultCurrency;
    int             recurringRuleId = 0;
    TransactionType type = TransactionType::Expense;
};
