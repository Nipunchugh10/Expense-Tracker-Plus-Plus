#pragma once
#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "Budget.h"
#include "CurrencyManager.h"
#include "Expense.h"
#include "FilterCriteria.h"
#include "Goal.h"
#include "RecurringRule.h"

// Complete ledger state, used to load/replace the tracker in one step.
struct LedgerData {
    std::vector<Expense>       expenses;
    std::vector<RecurringRule> rules;
    BudgetManager              budgets;
    std::vector<Goal>          goals;
    CurrencyManager            currency;
    int nextExpenseID = 1;
    int nextRuleID    = 1;
    int nextGoalID    = 1;
};

struct ImportResult {
    int imported = 0;
    int invalid = 0;
    int duplicatesSkipped = 0;
};

// Result of merging a backup into the current ledger (File > Restore from Backup).
struct MergeResult {
    int expensesAdded = 0;
    int expensesDuplicate = 0;
    int expensesInvalid = 0;
    int rulesAdded = 0;
    int rulesDuplicate = 0;
    int goalsAdded = 0;
    int goalsDuplicate = 0;
    int budgetsAdded = 0;
    int ratesAdded = 0;
};

// What a backup file contains, shown before it is restored.
struct LedgerSummary {
    int  expenses = 0;
    int  incomes = 0;
    int  transfers = 0;
    int  rules = 0;
    int  goals = 0;
    int  budgetMonths = 0;
    bool hasDates = false;
    Date firstDate;
    Date lastDate;
    std::string baseCurrency;
};
LedgerSummary SummarizeLedger(const LedgerData& data);

struct PendingOccurrences {
    int total = 0;
    int maxPerRule = 0;
};

struct GenerationResult {
    int generated = 0;
    int rulesCapped = 0;
};

// Totals are in the base currency. Rows whose currency has no exchange rate
// cannot be converted; they are excluded and counted (Step 2 / G2).
struct Totals {
    Money amount = 0;
    int   excluded = 0;
};

struct ForecastResult {
    enum class Kind { Past, Current, Future } kind = Kind::Current;
    int   daysInMonth = 0;
    int   today = 0;                             // day of month (current month only)
    std::vector<double> days;                    // 1..daysInMonth
    std::vector<double> actualCumulative;        // days 1..today (all days for past months)
    std::vector<double> forecastDays;            // today..daysInMonth
    std::vector<double> forecastCumulative;
    std::vector<double> forecastLow;
    std::vector<double> forecastHigh;
    Money  spentToDate = 0;
    Money  discretionaryToDate = 0;
    Money  scheduledRemaining = 0;
    Money  projectedTotal = 0;
    Money  dailyVelocity = 0;
    Money  budget = 0;
    bool   hasBudget = false;
    int    exhaustDay = 0;                       // 0 = budget not reached
    bool   alreadyExceeded = false;
    int    excluded = 0;
};

class ExpenseTracker {
public:
    ExpenseTracker() = default;

    // ── Expenses (every mutator validates, sets dirty and bumps revision) ──
    int  AddExpense(const Expense& proto);          // assigns a new ID; returns 0 on failure
    bool InsertExpense(const Expense& e);           // keeps e's ID (undo/redo); fails if taken
    bool UpdateExpense(const Expense& e);
    bool DeleteExpense(int id, bool recordSkip = true);
    const Expense* FindExpense(int id) const;
    const std::vector<Expense>& GetExpenses() const { return expenses; }
    std::vector<const Expense*> GetFilteredExpenses(const FilterCriteria& criteria) const;

    std::vector<size_t> FindLikelyDuplicates(const std::vector<Expense>& drafts) const;
    ImportResult ImportExpenses(const std::vector<Expense>& drafts, bool skipDuplicates);

    void ResetAll();
    void ReplaceAll(LedgerData&& data);
    // Adds a backup's contents: duplicates are skipped, incoming items get fresh
    // IDs, existing budgets and exchange rates are never overwritten.
    MergeResult MergeLedger(const LedgerData& incoming);
    void MarkDirty() { Touch(); }

    // ── Recurring rules ──
    int  AddRecurringRule(const RecurringRule& rule);   // assigns an ID; returns 0 on failure
    bool UpdateRecurringRule(const RecurringRule& rule);
    bool DeleteRecurringRule(int id);                   // generated expenses are kept and unlinked
    bool SetRuleActive(int id, bool active);
    const RecurringRule* FindRule(int id) const;
    const std::vector<RecurringRule>& GetRecurringRules() const { return recurringRules; }

    // Automatic recording (subscriptions with auto-record on). `nowMinutes` is
    // the time of day: a renewal on `upTo` counts only once its time has passed.
    PendingOccurrences CountPendingOccurrences(const Date& upTo, int nowMinutes = 24 * 60) const;
    GenerationResult   GenerateRecurringExpenses(const Date& upTo, int maxPerRule = 50000, int nowMinutes = 24 * 60);

    // Manual recording (auto-record off): renewals that are due but not yet
    // recorded, oldest first (at most `limit`).
    std::vector<Date> GetUnrecordedRenewals(const RecurringRule& rule, const Date& today, int nowMinutes,
                                            size_t limit = 50) const;
    int  RecordSubscriptionPayment(int ruleId, const Date& occurrence);   // returns the new transaction ID
    bool SkipSubscriptionPayment(int ruleId, const Date& occurrence);
    void               SkipRecurringBacklog(const Date& today);   // start from today instead of backfilling

    // ── Budgets (base currency) ──
    void SetOverallBudget(int year, int month, Money limit);
    void SetCategoryBudget(int year, int month, const std::string& category, Money limit);
    const BudgetManager& GetBudgetManager() const { return budgetManager; }

    // ── Goals ──
    int  AddGoal(const Goal& goal);
    bool UpdateGoal(const Goal& goal);
    bool DeleteGoal(int id);
    bool DepositToGoal(int id, Money delta);   // negative = withdraw; balance never below 0
    const Goal* FindGoal(int id) const;
    const std::vector<Goal>& GetGoals() const { return goals; }

    // ── Currency ──
    const CurrencyManager& GetCurrency() const { return currency; }
    const std::string& GetBaseCurrency() const { return currency.GetBaseCurrency(); }
    bool SetBaseCurrency(const std::string& code, bool convertBudgets);
    bool SetExchangeRateInBase(const std::string& code, double valueInBase);
    bool RemoveExchangeRate(const std::string& code);
    // Merges downloaded pivot rates: updates every currency already in the rate
    // table plus any currency in use that has no rate yet. Returns the number of
    // rates that changed (0 = nothing to save).
    int  ApplySyncedRates(const std::map<std::string, double>& pivotRates,
                          const std::string& asOf, const std::string& source);
    bool ToBase(const Expense& e, Money& out) const;

    // ── Analytics (base currency; month 0 = whole year) ──
    // type Expense = all spending (expenses + subscriptions); Subscription = subscriptions only.
    Totals GetTotal(int year, int month, TransactionType type) const;
    Money  GetMonthlyTotal(int year, int month) const;     // all spending
    Money  GetYearlyTotal(int year) const;                 // expenses only
    Money  GetMonthlyIncome(int year, int month) const;
    Money  GetYearlyIncome(int year) const;
    Money  GetNetSavings(int year, int month) const;
    std::optional<double> GetSavingsRate(int year, int month) const;   // nullopt when income is 0
    std::map<std::string, Money> GetCategoryBreakdown(int year, int month = 0,
                                                      TransactionType type = TransactionType::Expense) const;
    std::array<Money, 12> GetMonthlyTotals(int year, TransactionType type = TransactionType::Expense) const;
    std::vector<const Expense*> GetTopExpenses(int year, int count = 10, int month = 0) const;
    std::vector<const Expense*> GetRecentTransactions(size_t count) const;   // newest first
    std::map<std::string, Money> GetCurrencyBreakdown(int year) const;   // native amounts, expenses only
    int CountUnconvertible(int year, int month) const;
    ForecastResult ComputeForecast(int year, int month, const Date& today) const;

    // ── Dropdown data ──
    const std::vector<std::string>& GetCategories() const;
    const std::vector<std::string>& GetCurrencyOptions() const;   // rates + currencies in use
    std::string CanonicalCategory(const std::string& category) const;

    // ── State ──
    int  GetNextExpenseID() const { return nextExpenseID; }
    int  GetNextRuleID() const { return nextRuleID; }
    int  GetNextGoalID() const { return nextGoalID; }
    int  GetExpenseCount() const { return static_cast<int>(expenses.size()); }
    bool HasChanges() const { return dirty; }
    void ClearDirty() { dirty = false; }
    unsigned long long GetRevision() const { return revision; }
    const std::string& GetLastError() const { return lastError; }

private:
    void Touch() { dirty = true; ++revision; }
    Expense* FindExpenseMutable(int id);
    RecurringRule* FindRuleMutable(int id);
    ForecastResult ComputeForecastUncached(int year, int month, const Date& today) const;
    bool PrepareExpense(Expense& e);
    void CanonicalizeCategories();
    static std::string DuplicateKey(const Expense& e);

    // ── Derived-data cache (rebuilt lazily when `revision` changes) ──
    // Every screen reads totals each frame; with large ledgers recomputing them
    // from all transactions 60 times a second would stall the UI.
    struct PeriodAggregate {
        // Indexed by TransactionType. The Expense slot holds ALL spending
        // (expenses + subscriptions); the Subscription slot is the subset.
        Money total[kTransactionTypeCount] = {0, 0, 0, 0};   // base currency
        int   excluded[kTransactionTypeCount] = {0, 0, 0, 0}; // rows without an exchange rate
        std::map<std::string, Money> byCategory[kTransactionTypeCount];
        std::map<std::string, Money> nativeExpenseByCurrency;
    };
    void EnsureCache() const;
    mutable unsigned long long cacheRevision = ~0ULL;
    mutable std::map<int, PeriodAggregate> monthIndex;  // YYYYMM -> aggregate
    mutable std::map<long long, std::vector<const Expense*>> topCache;
    mutable std::vector<const Expense*> recentCache;
    mutable size_t recentCount = 0;
    mutable bool recentValid = false;
    mutable std::map<long long, ForecastResult> forecastCache;
    mutable std::vector<std::string> categoriesCache;
    mutable std::vector<std::string> currenciesCache;
    mutable bool listsValid = false;

    std::vector<Expense>       expenses;
    std::vector<RecurringRule> recurringRules;
    BudgetManager              budgetManager;
    std::vector<Goal>          goals;
    CurrencyManager            currency;
    int  nextExpenseID = 1;
    int  nextRuleID = 1;
    int  nextGoalID = 1;
    bool dirty = false;
    unsigned long long revision = 0;
    std::string lastError;
};
