#include "ExpenseTracker.h"
#include "SubscriptionDetector.h"
#include "Utils.h"
#include "Validation.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_set>

namespace {

long long OccurrenceKey(int ruleId, const Date& d) {
    return static_cast<long long>(ruleId) * 100000000LL + d.ToInt();
}

bool InPeriod(const Expense& e, int year, int month) {
    return e.GetYear() == year && (month == 0 || e.GetMonth() == month);
}

} // namespace

// ── Helpers ───────────────────────────────────────────────────────

Expense* ExpenseTracker::FindExpenseMutable(int id) {
    for (auto& e : expenses) {
        if (e.GetID() == id) return &e;
    }
    return nullptr;
}

const Expense* ExpenseTracker::FindExpense(int id) const {
    for (auto& e : expenses) {
        if (e.GetID() == id) return &e;
    }
    return nullptr;
}

RecurringRule* ExpenseTracker::FindRuleMutable(int id) {
    for (auto& r : recurringRules) {
        if (r.GetID() == id) return &r;
    }
    return nullptr;
}

const RecurringRule* ExpenseTracker::FindRule(int id) const {
    for (auto& r : recurringRules) {
        if (r.GetID() == id) return &r;
    }
    return nullptr;
}

std::string ExpenseTracker::CanonicalCategory(const std::string& category) const {
    std::string normalized = Validation::NormalizeCategory(category);
    std::string lower = Utils::ToLower(normalized);
    for (auto& e : expenses) {
        if (Utils::ToLower(e.GetCategory()) == lower) return e.GetCategory();
    }
    for (auto& r : recurringRules) {
        if (Utils::ToLower(r.GetCategory()) == lower) return r.GetCategory();
    }
    for (auto& [key, month] : budgetManager.GetAllCategoryBudgets()) {
        auto it = month.find(lower);
        if (it != month.end()) return it->second.displayName;
    }
    return normalized;
}

bool ExpenseTracker::PrepareExpense(Expense& e) {
    std::string err;
    if (!Validation::NormalizeExpense(e, err)) {
        lastError = err;
        return false;
    }
    e.SetCategory(CanonicalCategory(e.GetCategory()));
    if (e.GetRecurringRuleId() != 0 && !FindRule(e.GetRecurringRuleId())) e.SetRecurringRuleId(0);
    if (e.GetRecurringRuleId() != 0 && e.IsExpense()) e.SetType(TransactionType::Subscription);
    return true;
}

void ExpenseTracker::CanonicalizeCategories() {
    std::map<std::string, std::string> firstSeen;
    auto canon = [&](const std::string& cat) {
        std::string normalized = Validation::NormalizeCategory(cat);
        return firstSeen.emplace(Utils::ToLower(normalized), normalized).first->second;
    };
    for (auto& e : expenses) e.SetCategory(canon(e.GetCategory()));
    for (auto& r : recurringRules) r.SetCategory(canon(r.GetCategory()));
}

std::string ExpenseTracker::DuplicateKey(const Expense& e) {
    return std::to_string(e.GetDate().ToInt()) + "|" + Utils::ToLower(e.GetDescription()) + "|" +
           std::to_string(e.GetAmount()) + "|" + e.GetCurrency() + "|" +
           TransactionTypeToString(e.GetType());
}

// ── Expenses ──────────────────────────────────────────────────────

int ExpenseTracker::AddExpense(const Expense& proto) {
    Expense e = proto;
    if (!PrepareExpense(e)) return 0;
    e.SetID(nextExpenseID++);
    expenses.push_back(e);
    Touch();
    return e.GetID();
}

bool ExpenseTracker::InsertExpense(const Expense& proto) {
    if (proto.GetID() <= 0 || FindExpense(proto.GetID())) {
        lastError = "An expense with ID " + std::to_string(proto.GetID()) + " already exists.";
        return false;
    }
    Expense e = proto;
    if (!PrepareExpense(e)) return false;
    if (e.GetID() >= nextExpenseID) nextExpenseID = e.GetID() + 1;
    if (RecurringRule* rule = FindRuleMutable(e.GetRecurringRuleId())) rule->RemoveSkippedDate(e.GetDate());
    expenses.push_back(e);
    Touch();
    return true;
}

bool ExpenseTracker::UpdateExpense(const Expense& proto) {
    Expense* target = FindExpenseMutable(proto.GetID());
    if (!target) {
        lastError = "Expense #" + std::to_string(proto.GetID()) + " no longer exists.";
        return false;
    }
    Expense e = proto;
    if (!PrepareExpense(e)) return false;
    // Moving a generated occurrence must not let the old date regenerate.
    if (target->GetRecurringRuleId() != 0 && target->GetDate() != e.GetDate()) {
        if (RecurringRule* rule = FindRuleMutable(target->GetRecurringRuleId())) {
            rule->AddSkippedDate(target->GetDate());
        }
    }
    *target = e;
    Touch();
    return true;
}

bool ExpenseTracker::DeleteExpense(int id, bool recordSkip) {
    for (auto it = expenses.begin(); it != expenses.end(); ++it) {
        if (it->GetID() != id) continue;
        if (recordSkip) {
            if (RecurringRule* rule = FindRuleMutable(it->GetRecurringRuleId())) rule->AddSkippedDate(it->GetDate());
        }
        expenses.erase(it);
        Touch();
        return true;
    }
    lastError = "Expense #" + std::to_string(id) + " no longer exists.";
    return false;
}

std::vector<const Expense*> ExpenseTracker::GetFilteredExpenses(const FilterCriteria& criteria) const {
    std::vector<const Expense*> result;
    for (auto& e : expenses) {
        if (criteria.Matches(e)) result.push_back(&e);
    }
    return result;
}

std::vector<size_t> ExpenseTracker::FindLikelyDuplicates(const std::vector<Expense>& drafts) const {
    std::unordered_set<std::string> existing;
    for (auto& e : expenses) existing.insert(DuplicateKey(e));

    std::vector<size_t> result;
    for (size_t i = 0; i < drafts.size(); i++) {
        Expense e = drafts[i];
        std::string err;
        if (!Validation::NormalizeExpense(e, err)) continue;
        if (existing.count(DuplicateKey(e))) result.push_back(i);
    }
    return result;
}

ImportResult ExpenseTracker::ImportExpenses(const std::vector<Expense>& drafts, bool skipDuplicates) {
    ImportResult result;
    std::unordered_set<std::string> existing;
    for (auto& e : expenses) existing.insert(DuplicateKey(e));

    // Category index built once so large imports stay linear.
    std::map<std::string, std::string> categoryIndex;
    for (auto& e : expenses) categoryIndex.emplace(Utils::ToLower(e.GetCategory()), e.GetCategory());
    for (auto& r : recurringRules) categoryIndex.emplace(Utils::ToLower(r.GetCategory()), r.GetCategory());

    for (auto& draft : drafts) {
        Expense e = draft;
        e.SetRecurringRuleId(0);
        std::string err;
        if (!Validation::NormalizeExpense(e, err)) {
            result.invalid++;
            continue;
        }
        e.SetCategory(categoryIndex.emplace(Utils::ToLower(e.GetCategory()), e.GetCategory()).first->second);

        if (skipDuplicates && existing.count(DuplicateKey(e))) {
            result.duplicatesSkipped++;
            continue;
        }
        e.SetID(nextExpenseID++);   // never trust IDs from the file (P0-A2)
        expenses.push_back(e);
        result.imported++;
    }
    if (result.imported > 0) Touch();
    return result;
}

void ExpenseTracker::ResetAll() {
    expenses.clear();
    recurringRules.clear();
    budgetManager.Clear();
    goals.clear();
    currency.ResetToDefaults();
    nextExpenseID = 1;
    nextRuleID = 1;
    nextGoalID = 1;
    Touch();
}

void ExpenseTracker::ReplaceAll(LedgerData&& data) {
    expenses       = std::move(data.expenses);
    recurringRules = std::move(data.rules);
    budgetManager  = std::move(data.budgets);
    goals          = std::move(data.goals);
    currency       = std::move(data.currency);
    nextExpenseID  = data.nextExpenseID;
    nextRuleID     = data.nextRuleID;
    nextGoalID     = data.nextGoalID;
    CanonicalizeCategories();
    // Bills generated by older versions were plain expenses; they are subscriptions.
    int upgraded = 0;
    for (auto& e : expenses) {
        if (e.GetRecurringRuleId() != 0 && e.IsExpense()) {
            e.SetType(TransactionType::Subscription);
            upgraded++;
        }
    }
    dirty = upgraded > 0;   // persist the upgrade (auto-save writes it)
    ++revision;
}

// ── Recurring rules ───────────────────────────────────────────────

int ExpenseTracker::AddRecurringRule(const RecurringRule& rule) {
    RecurringRule r = rule;
    if (!r.Normalize(lastError)) return 0;
    r.SetCategory(CanonicalCategory(r.GetCategory()));
    r.SetID(nextRuleID++);   // 0 is reserved for "not recurring" (P0-D1)
    recurringRules.push_back(r);
    Touch();
    return r.GetID();
}

bool ExpenseTracker::UpdateRecurringRule(const RecurringRule& rule) {
    RecurringRule* target = FindRuleMutable(rule.GetID());
    if (!target) {
        lastError = "Subscription no longer exists.";
        return false;
    }
    RecurringRule r = rule;
    if (!r.Normalize(lastError)) return false;
    r.SetCategory(CanonicalCategory(r.GetCategory()));
    *target = r;
    Touch();
    return true;
}

bool ExpenseTracker::DeleteRecurringRule(int id) {
    auto it = std::find_if(recurringRules.begin(), recurringRules.end(),
                           [id](const RecurringRule& r) { return r.GetID() == id; });
    if (it == recurringRules.end()) {
        lastError = "Subscription no longer exists.";
        return false;
    }
    recurringRules.erase(it);
    // Past generated expenses are kept but unlinked (P0-D7).
    for (auto& e : expenses) {
        if (e.GetRecurringRuleId() == id) e.SetRecurringRuleId(0);
    }
    Touch();
    return true;
}

bool ExpenseTracker::SetRuleActive(int id, bool active) {
    RecurringRule* rule = FindRuleMutable(id);
    if (!rule) {
        lastError = "Subscription no longer exists.";
        return false;
    }
    if (active && !rule->IsActive()) {
        // Resuming never backfills the paused period.
        Date yesterday = Utils::AddDays(Date::Today(), -1);
        bool behind = !rule->HasGenerated() || rule->GetLastGeneratedThrough() < yesterday;
        if (behind && rule->GetStartDate() <= yesterday) rule->SetLastGeneratedThrough(yesterday);
    }
    rule->SetActive(active);
    Touch();
    return true;
}

int ExpenseTracker::AddDetectedSubscriptions(const std::vector<DetectedSubscription>& detected) {
    int created = 0;
    for (auto& d : detected) {
        int id = AddRecurringRule(d.ToRule());
        if (id == 0) continue;
        std::unordered_set<int> ids(d.expenseIds.begin(), d.expenseIds.end());
        for (auto& e : expenses) {
            if (e.GetRecurringRuleId() == 0 && ids.count(e.GetID())) e.SetRecurringRuleId(id);
        }
        created++;
    }
    if (created > 0) Touch();
    return created;
}

PendingOccurrences ExpenseTracker::CountPendingOccurrences(const Date& upTo, int nowMinutes) const {
    PendingOccurrences p;
    for (auto& rule : recurringRules) {
        if (!rule.IsActive() || !rule.IsAutoRecord()) continue;
        Date from = rule.HasGenerated() ? Utils::AddDays(rule.GetLastGeneratedThrough(), 1) : rule.GetStartDate();
        Date to = std::min(rule.DueThrough(upTo, nowMinutes), rule.GetEndDate());
        if (to < from) continue;
        int n = static_cast<int>(rule.GetOccurrences(from, to).size());
        p.total += n;
        p.maxPerRule = std::max(p.maxPerRule, n);
    }
    return p;
}

GenerationResult ExpenseTracker::GenerateRecurringExpenses(const Date& upTo, int maxPerRule, int nowMinutes) {
    GenerationResult result;
    std::unordered_set<long long> existing;
    for (auto& e : expenses) {
        if (e.GetRecurringRuleId() != 0) existing.insert(OccurrenceKey(e.GetRecurringRuleId(), e.GetDate()));
    }

    bool changed = false;
    for (auto& rule : recurringRules) {
        if (!rule.IsActive() || !rule.IsAutoRecord()) continue;   // manual rules are recorded by the user
        Date from = rule.HasGenerated() ? Utils::AddDays(rule.GetLastGeneratedThrough(), 1) : rule.GetStartDate();
        Date to = std::min(rule.DueThrough(upTo, nowMinutes), rule.GetEndDate());
        if (to < from) continue;

        size_t cap = maxPerRule > 0 ? static_cast<size_t>(maxPerRule) : 100000;
        auto dates = rule.GetOccurrences(from, to, cap + 1);
        bool capped = dates.size() > cap;
        if (capped) {
            dates.resize(cap);
            result.rulesCapped++;
        }

        for (auto& d : dates) {
            if (rule.IsSkipped(d) || existing.count(OccurrenceKey(rule.GetID(), d))) continue;
            Expense e(nextExpenseID++, rule.GetDescription(), rule.GetAmount(), d,
                      rule.GetCategory(), rule.GetCurrency(), rule.GetID(), TransactionType::Subscription);
            expenses.push_back(e);
            existing.insert(OccurrenceKey(rule.GetID(), d));
            result.generated++;
        }
        Date through = (capped && !dates.empty()) ? dates.back() : to;
        if (!rule.HasGenerated() || rule.GetLastGeneratedThrough() != through) {
            rule.SetLastGeneratedThrough(through);
            changed = true;
        }
    }
    if (changed || result.generated > 0) Touch();
    return result;
}

std::vector<Date> ExpenseTracker::GetUnrecordedRenewals(const RecurringRule& rule, const Date& today, int nowMinutes,
                                                        size_t limit) const {
    std::vector<Date> result;
    if (!rule.IsActive() || rule.IsAutoRecord()) return result;
    Date from = rule.HasGenerated() ? Utils::AddDays(rule.GetLastGeneratedThrough(), 1) : rule.GetStartDate();
    Date to = std::min(rule.DueThrough(today, nowMinutes), rule.GetEndDate());
    if (to < from) return result;
    for (auto& d : rule.GetOccurrences(from, to, limit + 64)) {
        if (rule.IsSkipped(d)) continue;
        result.push_back(d);
        if (result.size() >= limit) break;
    }
    return result;
}

int ExpenseTracker::RecordSubscriptionPayment(int ruleId, const Date& occurrence) {
    RecurringRule* rule = FindRuleMutable(ruleId);
    if (!rule) {
        lastError = "Subscription no longer exists.";
        return 0;
    }
    Expense e(0, rule->GetDescription(), rule->GetAmount(), occurrence, rule->GetCategory(), rule->GetCurrency(),
              ruleId, TransactionType::Subscription);
    int id = AddExpense(e);
    if (id == 0) return 0;
    if (!rule->HasGenerated() || rule->GetLastGeneratedThrough() < occurrence) rule->SetLastGeneratedThrough(occurrence);
    Touch();
    return id;
}

bool ExpenseTracker::SkipSubscriptionPayment(int ruleId, const Date& occurrence) {
    RecurringRule* rule = FindRuleMutable(ruleId);
    if (!rule) {
        lastError = "Subscription no longer exists.";
        return false;
    }
    rule->AddSkippedDate(occurrence);
    if (!rule->HasGenerated() || rule->GetLastGeneratedThrough() < occurrence) rule->SetLastGeneratedThrough(occurrence);
    Touch();
    return true;
}

void ExpenseTracker::SkipRecurringBacklog(const Date& today) {
    Date yesterday = Utils::AddDays(today, -1);
    bool changed = false;
    for (auto& rule : recurringRules) {
        if (!rule.IsActive() || rule.GetStartDate() > yesterday) continue;
        if (!rule.HasGenerated() || rule.GetLastGeneratedThrough() < yesterday) {
            rule.SetLastGeneratedThrough(yesterday);
            changed = true;
        }
    }
    if (changed) Touch();
}

// ── Budgets ───────────────────────────────────────────────────────

void ExpenseTracker::SetOverallBudget(int year, int month, Money limit) {
    std::string err;
    if (month < 1 || month > 12 || year != Validation::ClampYear(year) || !Validation::ValidateMoney(limit, err)) return;
    if (budgetManager.GetOverallBudget(year, month) == limit) return;
    budgetManager.SetOverallBudget(year, month, limit);
    Touch();
}

void ExpenseTracker::SetCategoryBudget(int year, int month, const std::string& category, Money limit) {
    std::string err;
    if (month < 1 || month > 12 || year != Validation::ClampYear(year) || !Validation::ValidateMoney(limit, err)) return;
    std::string cat = CanonicalCategory(category);
    if (budgetManager.GetCategoryBudget(year, month, cat) == limit) return;
    budgetManager.SetCategoryBudget(year, month, cat, limit);
    Touch();
}

// ── Goals ─────────────────────────────────────────────────────────

int ExpenseTracker::AddGoal(const Goal& goal) {
    Goal g = goal;
    if (!g.Normalize(lastError)) return 0;
    g.id = nextGoalID++;
    goals.push_back(g);
    Touch();
    return g.id;
}

bool ExpenseTracker::UpdateGoal(const Goal& goal) {
    for (auto& g : goals) {
        if (g.id != goal.id) continue;
        Goal copy = goal;
        if (!copy.Normalize(lastError)) return false;
        g = copy;
        Touch();
        return true;
    }
    lastError = "Goal no longer exists.";
    return false;
}

bool ExpenseTracker::DeleteGoal(int id) {
    auto it = std::find_if(goals.begin(), goals.end(), [id](const Goal& g) { return g.id == id; });
    if (it == goals.end()) {
        lastError = "Goal no longer exists.";
        return false;
    }
    goals.erase(it);
    Touch();
    return true;
}

bool ExpenseTracker::DepositToGoal(int id, Money delta) {
    for (auto& g : goals) {
        if (g.id != id) continue;
        Money next = g.currentAmount + delta;
        if (next < 0) {
            lastError = "Cannot withdraw more than the goal's current balance.";
            return false;
        }
        if (!Validation::ValidateMoney(next, lastError)) return false;
        g.currentAmount = next;
        Touch();
        return true;
    }
    lastError = "Goal no longer exists.";
    return false;
}

const Goal* ExpenseTracker::FindGoal(int id) const {
    for (auto& g : goals) {
        if (g.id == id) return &g;
    }
    return nullptr;
}

// ── Currency ──────────────────────────────────────────────────────

bool ExpenseTracker::SetBaseCurrency(const std::string& code, bool convertBudgets) {
    std::string old = currency.GetBaseCurrency();
    if (code == old) return true;
    auto factor = currency.Factor(old, code);
    if (!currency.SetBaseCurrency(code)) {
        lastError = "No exchange rate is defined for " + code + ".";
        return false;
    }
    if (convertBudgets && factor) budgetManager.ScaleAll(*factor);
    Touch();
    return true;
}

bool ExpenseTracker::SetExchangeRateInBase(const std::string& code, double valueInBase) {
    std::string cur;
    if (!Validation::NormalizeCurrency(code, cur)) {
        lastError = "Currency must be a 3-letter code such as INR or USD.";
        return false;
    }
    if (!currency.SetRateInBase(cur, valueInBase)) {
        lastError = "Rate must be a positive number (and the base currency is always 1).";
        return false;
    }
    currency.SetAsOf(Date::Today().ToString());
    currency.SetSource("manual");
    Touch();
    return true;
}

bool ExpenseTracker::RemoveExchangeRate(const std::string& code) {
    if (!currency.RemoveRate(code)) {
        lastError = "USD (the pivot) and the base currency cannot be removed.";
        return false;
    }
    Touch();
    return true;
}

int ExpenseTracker::ApplySyncedRates(const std::map<std::string, double>& pivotRates,
                                     const std::string& asOf, const std::string& source) {
    std::set<std::string> wanted;
    for (auto& [code, rate] : currency.GetAllRates()) wanted.insert(code);
    for (auto& e : expenses) wanted.insert(e.GetCurrency());
    for (auto& r : recurringRules) wanted.insert(r.GetCurrency());
    for (auto& g : goals) wanted.insert(g.currency);
    wanted.insert(currency.GetBaseCurrency());

    int changed = 0;
    for (auto& code : wanted) {
        auto it = pivotRates.find(code);
        if (it == pivotRates.end()) continue;
        auto current = currency.GetPivotRate(code);
        if (current && std::fabs(*current - it->second) <= 1e-12 * std::max(1.0, it->second)) continue;
        if (currency.SetPivotRate(code, it->second)) changed++;
    }
    bool metaChanged = currency.GetAsOf() != asOf || currency.GetSource() != source;
    currency.SetAsOf(asOf);
    currency.SetSource(source);
    if (changed > 0 || metaChanged) Touch();
    return changed;
}

bool ExpenseTracker::ToBase(const Expense& e, Money& out) const {
    return currency.ConvertToBase(e.GetAmount(), e.GetCurrency(), out);
}

// ── Analytics ─────────────────────────────────────────────────────

ForecastResult ExpenseTracker::ComputeForecast(int year, int month, const Date& today) const {
    EnsureCache();
    long long key = (static_cast<long long>(year) * 100 + month) * 100000000LL + today.ToInt();
    auto cached = forecastCache.find(key);
    if (cached != forecastCache.end()) return cached->second;
    ForecastResult r = ComputeForecastUncached(year, month, today);
    forecastCache[key] = r;
    return r;
}

ForecastResult ExpenseTracker::ComputeForecastUncached(int year, int month, const Date& today) const {
    ForecastResult r;
    r.daysInMonth = Utils::DaysInMonth(year, month);
    if (r.daysInMonth == 0) return r;
    const int N = r.daysInMonth;
    const Date first = {year, month, 1};
    const Date last = Utils::EndOfMonth(year, month);

    if (last < today)       r.kind = ForecastResult::Kind::Past;
    else if (first > today) r.kind = ForecastResult::Kind::Future;
    else                    r.kind = ForecastResult::Kind::Current;

    const int D = r.kind == ForecastResult::Kind::Current ? today.day
                : r.kind == ForecastResult::Kind::Past ? N : 0;
    r.today = D;

    Money overall = budgetManager.GetOverallBudget(year, month);
    r.budget = overall > 0 ? overall : budgetManager.GetCategoryBudgetSum(year, month);
    r.hasBudget = r.budget > 0;

    std::vector<Money> daily(static_cast<size_t>(N) + 1, 0);
    std::vector<Money> scheduled(static_cast<size_t>(N) + 1, 0);
    for (auto& e : expenses) {
        if (!e.IsSpending() || e.GetYear() != year || e.GetMonth() != month) continue;
        Money v;
        if (!ToBase(e, v)) { r.excluded++; continue; }
        size_t d = static_cast<size_t>(e.GetDate().day);
        if (e.GetDate().day <= D) {
            daily[d] += v;
            r.spentToDate += v;
            // Recurring bills are fixed costs, not discretionary burn (G8).
            if (e.IsExpense() && e.GetRecurringRuleId() == 0) r.discretionaryToDate += v;
        } else {
            scheduled[d] += v;
        }
    }

    // Occurrences not generated yet (after today) are added as scheduled bills.
    if (r.kind != ForecastResult::Kind::Past) {
        std::unordered_set<long long> existing;
        for (auto& e : expenses) {
            if (e.GetRecurringRuleId() != 0) existing.insert(OccurrenceKey(e.GetRecurringRuleId(), e.GetDate()));
        }
        Date from = r.kind == ForecastResult::Kind::Current ? Utils::AddDays(today, 1) : first;
        for (auto& rule : recurringRules) {
            if (!rule.IsActive()) continue;
            for (auto& d : rule.GetOccurrences(from, last)) {
                if (rule.IsSkipped(d) || existing.count(OccurrenceKey(rule.GetID(), d))) continue;
                if (rule.HasGenerated() && d <= rule.GetLastGeneratedThrough()) continue;
                Money v;
                if (!currency.ConvertToBase(rule.GetAmount(), rule.GetCurrency(), v)) { r.excluded++; continue; }
                scheduled[static_cast<size_t>(d.day)] += v;
            }
        }
    }

    for (int d = 1; d <= N; d++) r.days.push_back(d);

    Money cum = 0;
    for (int d = 1; d <= D; d++) {
        cum += daily[static_cast<size_t>(d)];
        r.actualCumulative.push_back(MoneyUtil::ToMajor(cum));
        if (r.hasBudget && r.exhaustDay == 0 && cum >= r.budget) {
            r.exhaustDay = d;
            r.alreadyExceeded = true;
        }
    }

    for (int d = D + 1; d <= N; d++) r.scheduledRemaining += scheduled[static_cast<size_t>(d)];

    if (r.kind == ForecastResult::Kind::Past) {
        r.projectedTotal = r.spentToDate;
        return r;
    }

    r.dailyVelocity = D > 0 ? r.discretionaryToDate / D : 0;
    const double v = MoneyUtil::ToMajor(r.dailyVelocity);
    Money fc = r.spentToDate;
    double lo = MoneyUtil::ToMajor(fc), hi = lo;
    r.forecastDays.push_back(D);
    r.forecastCumulative.push_back(MoneyUtil::ToMajor(fc));
    r.forecastLow.push_back(lo);
    r.forecastHigh.push_back(hi);
    for (int d = D + 1; d <= N; d++) {
        Money sched = scheduled[static_cast<size_t>(d)];
        fc += r.dailyVelocity + sched;
        lo += v * 0.75 + MoneyUtil::ToMajor(sched);
        hi += v * 1.25 + MoneyUtil::ToMajor(sched);
        r.forecastDays.push_back(d);
        r.forecastCumulative.push_back(MoneyUtil::ToMajor(fc));
        r.forecastLow.push_back(lo);
        r.forecastHigh.push_back(hi);
        if (r.hasBudget && r.exhaustDay == 0 && fc >= r.budget) r.exhaustDay = d;
    }
    r.projectedTotal = fc;
    return r;
}

// ── Cached analytics ──────────────────────────────────────────────

void ExpenseTracker::EnsureCache() const {
    if (cacheRevision == revision) return;
    cacheRevision = revision;
    monthIndex.clear();
    topCache.clear();
    forecastCache.clear();
    recentValid = false;
    listsValid = false;
    for (auto& e : expenses) {
        PeriodAggregate& agg = monthIndex[e.GetYear() * 100 + e.GetMonth()];
        size_t t = static_cast<size_t>(e.GetType());
        const size_t spend = static_cast<size_t>(TransactionType::Expense);
        Money v;
        if (ToBase(e, v)) {
            agg.total[t] += v;
            agg.byCategory[t][e.GetCategory()] += v;
            if (e.IsSubscription() || e.IsTransfer()) {
                agg.total[spend] += v;
                agg.byCategory[spend][e.GetCategory()] += v;
            }
        } else {
            agg.excluded[t]++;
            if (e.IsSubscription() || e.IsTransfer()) agg.excluded[spend]++;
        }
        if (e.IsSpending()) agg.nativeExpenseByCurrency[e.GetCurrency()] += e.GetAmount();
    }
}

Totals ExpenseTracker::GetTotal(int year, int month, TransactionType type) const {
    EnsureCache();
    Totals t;
    size_t ti = static_cast<size_t>(type);
    int from = month == 0 ? 1 : month, to = month == 0 ? 12 : month;
    for (int m = from; m <= to; m++) {
        auto it = monthIndex.find(year * 100 + m);
        if (it == monthIndex.end()) continue;
        t.amount += it->second.total[ti];
        t.excluded += it->second.excluded[ti];
    }
    return t;
}

Money ExpenseTracker::GetMonthlyTotal(int year, int month) const {
    return GetTotal(year, month, TransactionType::Expense).amount;
}

Money ExpenseTracker::GetYearlyTotal(int year) const {
    return GetTotal(year, 0, TransactionType::Expense).amount;
}

Money ExpenseTracker::GetMonthlyIncome(int year, int month) const {
    return GetTotal(year, month, TransactionType::Income).amount;
}

Money ExpenseTracker::GetYearlyIncome(int year) const {
    return GetTotal(year, 0, TransactionType::Income).amount;
}

Money ExpenseTracker::GetNetSavings(int year, int month) const {
    return GetTotal(year, month, TransactionType::Income).amount -
           GetTotal(year, month, TransactionType::Expense).amount;
}

std::optional<double> ExpenseTracker::GetSavingsRate(int year, int month) const {
    Money income = GetTotal(year, month, TransactionType::Income).amount;
    if (income <= 0) return std::nullopt;
    Money net = income - GetTotal(year, month, TransactionType::Expense).amount;
    return static_cast<double>(net) / static_cast<double>(income) * 100.0;
}

std::map<std::string, Money> ExpenseTracker::GetCategoryBreakdown(int year, int month, TransactionType type) const {
    EnsureCache();
    std::map<std::string, Money> breakdown;
    size_t ti = static_cast<size_t>(type);
    int from = month == 0 ? 1 : month, to = month == 0 ? 12 : month;
    for (int m = from; m <= to; m++) {
        auto it = monthIndex.find(year * 100 + m);
        if (it == monthIndex.end()) continue;
        for (auto& [cat, v] : it->second.byCategory[ti]) breakdown[cat] += v;
    }
    return breakdown;
}

std::array<Money, 12> ExpenseTracker::GetMonthlyTotals(int year, TransactionType type) const {
    std::array<Money, 12> totals{};
    for (int m = 1; m <= 12; m++) totals[static_cast<size_t>(m - 1)] = GetTotal(year, m, type).amount;
    return totals;
}

std::vector<const Expense*> ExpenseTracker::GetTopExpenses(int year, int count, int month) const {
    EnsureCache();
    long long key = (static_cast<long long>(year) * 100 + month) * 100000LL + count;
    auto cached = topCache.find(key);
    if (cached != topCache.end()) return cached->second;

    std::vector<std::pair<Money, const Expense*>> ranked;
    for (auto& e : expenses) {
        if (!e.IsSpending() || !InPeriod(e, year, month)) continue;
        Money v;
        if (ToBase(e, v)) ranked.emplace_back(v, &e);
    }
    auto better = [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first > b.first;
        return a.second->GetID() < b.second->GetID();
    };
    size_t n = std::min(ranked.size(), static_cast<size_t>(std::max(count, 0)));
    std::partial_sort(ranked.begin(), ranked.begin() + static_cast<std::ptrdiff_t>(n), ranked.end(), better);
    std::vector<const Expense*> result;
    for (size_t i = 0; i < n; i++) result.push_back(ranked[i].second);
    topCache[key] = result;
    return result;
}

std::vector<const Expense*> ExpenseTracker::GetRecentTransactions(size_t count) const {
    EnsureCache();
    if (recentValid && recentCount == count) return recentCache;
    std::vector<const Expense*> all;
    all.reserve(expenses.size());
    for (auto& e : expenses) all.push_back(&e);
    size_t n = std::min(count, all.size());
    std::partial_sort(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(n), all.end(),
                      [](const Expense* a, const Expense* b) {
                          if (a->GetDate() != b->GetDate()) return a->GetDate() > b->GetDate();
                          return a->GetID() > b->GetID();
                      });
    all.resize(n);
    recentCache = std::move(all);
    recentCount = count;
    recentValid = true;
    return recentCache;
}

std::map<std::string, Money> ExpenseTracker::GetCurrencyBreakdown(int year) const {
    EnsureCache();
    std::map<std::string, Money> breakdown;
    for (int m = 1; m <= 12; m++) {
        auto it = monthIndex.find(year * 100 + m);
        if (it == monthIndex.end()) continue;
        for (auto& [code, v] : it->second.nativeExpenseByCurrency) breakdown[code] += v;
    }
    return breakdown;
}

int ExpenseTracker::CountUnconvertible(int year, int month) const {
    return GetTotal(year, month, TransactionType::Expense).excluded +
           GetTotal(year, month, TransactionType::Income).excluded;
}

// ── Dropdown data ─────────────────────────────────────────────────

const std::vector<std::string>& ExpenseTracker::GetCategories() const {
    EnsureCache();
    if (!listsValid) {
        std::map<std::string, std::string> cats;   // lower -> display
        for (auto& e : expenses) cats.emplace(Utils::ToLower(e.GetCategory()), e.GetCategory());
        for (auto& r : recurringRules) cats.emplace(Utils::ToLower(r.GetCategory()), r.GetCategory());
        for (auto& [key, month] : budgetManager.GetAllCategoryBudgets()) {
            for (auto& [lower, entry] : month) cats.emplace(lower, entry.displayName);
        }
        categoriesCache.clear();
        for (auto& [lower, display] : cats) {
            if (!display.empty()) categoriesCache.push_back(display);
        }

        std::set<std::string> codes;
        for (auto& [code, rate] : currency.GetAllRates()) codes.insert(code);
        for (auto& e : expenses) codes.insert(e.GetCurrency());
        for (auto& r : recurringRules) codes.insert(r.GetCurrency());
        for (auto& g : goals) codes.insert(g.currency);
        codes.insert(kDefaultCurrency);
        currenciesCache.assign(codes.begin(), codes.end());
        listsValid = true;
    }
    return categoriesCache;
}

const std::vector<std::string>& ExpenseTracker::GetCurrencyOptions() const {
    GetCategories();   // builds both lists
    return currenciesCache;
}

// ── Backups: summary and merge ────────────────────────────────────

LedgerSummary SummarizeLedger(const LedgerData& data) {
    LedgerSummary s;
    for (auto& e : data.expenses) {
        if (e.IsIncome()) s.incomes++;
        else if (e.IsTransfer()) s.transfers++;
        else if (e.IsSavings()) s.savings++;
        else s.expenses++;
        if (!s.hasDates || e.GetDate() < s.firstDate) s.firstDate = e.GetDate();
        if (!s.hasDates || e.GetDate() > s.lastDate) s.lastDate = e.GetDate();
        s.hasDates = true;
    }
    s.rules = static_cast<int>(data.rules.size());
    s.goals = static_cast<int>(data.goals.size());
    std::set<int> months;
    for (auto& [key, limit] : data.budgets.GetOverallBudgets()) months.insert(key);
    for (auto& [key, cats] : data.budgets.GetAllCategoryBudgets()) months.insert(key);
    s.budgetMonths = static_cast<int>(months.size());
    s.baseCurrency = data.currency.GetBaseCurrency();
    return s;
}

MergeResult ExpenseTracker::MergeLedger(const LedgerData& incoming) {
    MergeResult r;

    // Subscriptions: identical rules map to the existing one; others get new IDs.
    auto ruleKey = [](const RecurringRule& x) {
        return Utils::ToLower(x.GetDescription()) + "|" + std::to_string(x.GetAmount()) + "|" + x.GetCurrency() + "|" +
               RecurringRule::FrequencyToString(x.GetFrequency()) + "|" + std::to_string(x.GetIntervalDays()) + "|" +
               std::to_string(x.GetStartDate().ToInt());
    };
    std::map<std::string, int> existingRules;
    for (auto& x : recurringRules) existingRules.emplace(ruleKey(x), x.GetID());
    std::map<int, int> ruleIdMap;   // incoming id -> id in this ledger
    for (auto& in : incoming.rules) {
        auto it = existingRules.find(ruleKey(in));
        if (it != existingRules.end()) {
            ruleIdMap[in.GetID()] = it->second;
            r.rulesDuplicate++;
            continue;
        }
        RecurringRule copy = in;
        std::string err;
        if (!copy.Normalize(err)) continue;
        copy.SetCategory(CanonicalCategory(copy.GetCategory()));
        copy.SetID(nextRuleID++);
        ruleIdMap[in.GetID()] = copy.GetID();
        existingRules.emplace(ruleKey(copy), copy.GetID());
        recurringRules.push_back(copy);
        r.rulesAdded++;
    }

    // Transactions: skip exact duplicates, always assign fresh IDs.
    std::unordered_set<std::string> existing;
    for (auto& e : expenses) existing.insert(DuplicateKey(e));
    std::map<std::string, std::string> categoryIndex;
    for (auto& e : expenses) categoryIndex.emplace(Utils::ToLower(e.GetCategory()), e.GetCategory());
    for (auto& x : recurringRules) categoryIndex.emplace(Utils::ToLower(x.GetCategory()), x.GetCategory());
    for (auto& in : incoming.expenses) {
        Expense e = in;
        std::string err;
        if (!Validation::NormalizeExpense(e, err)) {
            r.expensesInvalid++;
            continue;
        }
        e.SetCategory(categoryIndex.emplace(Utils::ToLower(e.GetCategory()), e.GetCategory()).first->second);
        // Link to the merged rule (and settle the type) BEFORE the duplicate check, so the key
        // matches what an earlier merge of the same backup stored.
        auto mapped = ruleIdMap.find(e.GetRecurringRuleId());
        e.SetRecurringRuleId(mapped != ruleIdMap.end() ? mapped->second : 0);
        if (e.GetRecurringRuleId() != 0 && e.IsExpense()) e.SetType(TransactionType::Subscription);
        if (!existing.insert(DuplicateKey(e)).second) {
            r.expensesDuplicate++;
            continue;
        }
        e.SetID(nextExpenseID++);
        expenses.push_back(e);
        r.expensesAdded++;
    }

    // Goals: same title (case-insensitive) counts as the same goal.
    std::set<std::string> goalTitles;
    for (auto& g : goals) goalTitles.insert(Utils::ToLower(g.title));
    for (auto& in : incoming.goals) {
        if (!goalTitles.insert(Utils::ToLower(in.title)).second) {
            r.goalsDuplicate++;
            continue;
        }
        Goal g = in;
        std::string err;
        if (!g.Normalize(err)) continue;
        g.id = nextGoalID++;
        goals.push_back(g);
        r.goalsAdded++;
    }

    // Budgets: only fill months/categories that have no budget yet.
    for (auto& [key, limit] : incoming.budgets.GetOverallBudgets()) {
        int y, m;
        BudgetManager::SplitKey(key, y, m);
        if (!budgetManager.HasOverallBudget(y, m)) {
            budgetManager.SetOverallBudget(y, m, limit);
            r.budgetsAdded++;
        }
    }
    for (auto& [key, cats] : incoming.budgets.GetAllCategoryBudgets()) {
        int y, m;
        BudgetManager::SplitKey(key, y, m);
        for (auto& [lower, entry] : cats) {
            if (!budgetManager.HasCategoryBudget(y, m, entry.displayName)) {
                budgetManager.SetCategoryBudget(y, m, CanonicalCategory(entry.displayName), entry.limit);
                r.budgetsAdded++;
            }
        }
    }

    // Exchange rates: add currencies we do not know; never override current rates.
    for (auto& [code, rate] : incoming.currency.GetAllRates()) {
        if (!currency.HasRate(code) && currency.SetPivotRate(code, rate)) r.ratesAdded++;
    }

    if (r.expensesAdded || r.rulesAdded || r.goalsAdded || r.budgetsAdded || r.ratesAdded) Touch();
    return r;
}
