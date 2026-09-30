// Recurring engine, currency, budgets, cash flow, goals, forecast,
// auto-categorization, undo/redo and reports (Phase 0 group D + Steps 1-10).
#include "TestFramework.h"
#include "TestHelpers.h"
#include "AutoCategorizer.h"
#include "CommandManager.h"
#include "ReportGenerator.h"
#include "Utils.h"
#include <chrono>
#include <cmath>

namespace {

RecurringRule MakeRule(const std::string& name, double amount, Frequency f, const Date& start,
                       const Date& end = {2100, 12, 31}) {
    return RecurringRule(0, name, MoneyUtil::FromMajor(amount), "Bills", "INR", f, start, end);
}

int CountForRule(const ExpenseTracker& t, int ruleId) {
    int n = 0;
    for (auto& e : t.GetExpenses()) {
        if (e.GetRecurringRuleId() == ruleId) n++;
    }
    return n;
}

} // namespace

// ── Recurring engine (P0-D1..D7, Step 1) ──────────────────────────

TEST_CASE("Rule IDs are assigned sequentially and survive") {
    ExpenseTracker t;
    CHECK_EQ(t.AddRecurringRule(MakeRule("A", 1, Frequency::Monthly, {2025, 1, 1})), 1);
    CHECK_EQ(t.AddRecurringRule(MakeRule("B", 1, Frequency::Monthly, {2025, 1, 1})), 2);
    CHECK_EQ(t.AddRecurringRule(MakeRule("C", 1, Frequency::Monthly, {2025, 1, 1})), 3);
    CHECK_EQ(t.AddRecurringRule(MakeRule("", 1, Frequency::Monthly, {2025, 1, 1})), 0);
    CHECK_EQ(t.AddRecurringRule(MakeRule("Bad", 1, Frequency::Monthly, {2025, 5, 1}, {2025, 1, 1})), 0);
}

TEST_CASE("Month-end schedules do not drift") {
    RecurringRule r = MakeRule("Rent", 1, Frequency::Monthly, {2024, 1, 31});
    auto dates = r.GetOccurrences({2024, 1, 1}, {2024, 5, 31});
    REQUIRE(dates.size() == 5);
    CHECK(dates[1] == (Date{2024, 2, 29}));
    CHECK(dates[2] == (Date{2024, 3, 31}));
    CHECK(dates[3] == (Date{2024, 4, 30}));
    CHECK(dates[4] == (Date{2024, 5, 31}));

    RecurringRule leap = MakeRule("Leap", 1, Frequency::Yearly, {2024, 2, 29});
    auto years = leap.GetOccurrences({2024, 1, 1}, {2028, 12, 31});
    REQUIRE(years.size() == 5);
    CHECK(years[1] == (Date{2025, 2, 28}));
    CHECK(years[4] == (Date{2028, 2, 29}));
}

TEST_CASE("Occurrence search starts mid-range correctly for every frequency") {
    RecurringRule weekly = MakeRule("W", 1, Frequency::Weekly, {2025, 1, 1});
    auto w = weekly.GetOccurrences({2025, 1, 2}, {2025, 1, 31});
    REQUIRE(!w.empty());
    CHECK(w.front() == (Date{2025, 1, 8}));
    CHECK_EQ(w.size(), size_t(4));

    RecurringRule daily = MakeRule("D", 1, Frequency::Daily, {1990, 1, 1});
    auto d = daily.GetOccurrences({2025, 3, 1}, {2025, 3, 31});
    CHECK_EQ(d.size(), size_t(31));

    Date next;
    RecurringRule yearly = MakeRule("Y", 1, Frequency::Yearly, {2020, 6, 15}, {2026, 1, 1});
    CHECK(yearly.NextOccurrenceOnOrAfter({2025, 6, 16}, next) == false);
    CHECK(yearly.NextOccurrenceOnOrAfter({2025, 6, 1}, next) && next == (Date{2025, 6, 15}));
    CHECK(yearly.IsDueSoon({2025, 6, 10}, 7));
    CHECK(!yearly.IsDueSoon({2025, 6, 1}, 7));
}

TEST_CASE("Deleted or moved occurrences are not regenerated") {
    ExpenseTracker t;
    int rid = t.AddRecurringRule(MakeRule("Gym", 30, Frequency::Monthly, {2025, 1, 10}));
    auto g1 = t.GenerateRecurringExpenses({2025, 3, 31});
    CHECK_EQ(g1.generated, 3);

    int feb = 0, mar = 0;
    for (auto& e : t.GetExpenses()) {
        if (e.GetMonth() == 2) feb = e.GetID();
        if (e.GetMonth() == 3) mar = e.GetID();
    }
    CHECK(t.DeleteExpense(feb));
    Expense moved = *t.FindExpense(mar);
    moved.SetDate({2025, 3, 12});
    CHECK(t.UpdateExpense(moved));

    auto g2 = t.GenerateRecurringExpenses({2025, 3, 31});
    CHECK_EQ(g2.generated, 0);
    CHECK_EQ(CountForRule(t, rid), 2);
    // Running again later only adds new months.
    auto g3 = t.GenerateRecurringExpenses({2025, 4, 30});
    CHECK_EQ(g3.generated, 1);
}

TEST_CASE("Backfill is bounded, fast and can be skipped") {
    ExpenseTracker t;
    Date today = {2025, 6, 30};
    t.AddRecurringRule(MakeRule("Coffee", 3, Frequency::Daily, Utils::AddDays(today, -5 * 365)));
    auto pending = t.CountPendingOccurrences(today);
    CHECK(pending.maxPerRule > 366);

    auto start = std::chrono::steady_clock::now();
    auto result = t.GenerateRecurringExpenses(today);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    CHECK_EQ(result.generated, pending.total);
    CHECK(ms < 1000);

    ExpenseTracker s;
    s.AddRecurringRule(MakeRule("Coffee", 3, Frequency::Daily, Utils::AddDays(today, -5 * 365)));
    s.SkipRecurringBacklog(today);
    CHECK_EQ(s.CountPendingOccurrences(today).total, 1);
    CHECK_EQ(s.GenerateRecurringExpenses(today).generated, 1);

    ExpenseTracker c;
    c.AddRecurringRule(MakeRule("Coffee", 3, Frequency::Daily, {2025, 1, 1}));
    auto capped = c.GenerateRecurringExpenses({2025, 12, 31}, 100);
    CHECK_EQ(capped.generated, 100);
    CHECK_EQ(capped.rulesCapped, 1);
    CHECK_EQ(c.GenerateRecurringExpenses({2025, 12, 31}, 100).generated, 100);   // continues where it stopped
}

TEST_CASE("Deleting a rule keeps and unlinks its expenses") {
    ExpenseTracker t;
    int rid = t.AddRecurringRule(MakeRule("Phone", 10, Frequency::Monthly, {2025, 1, 1}));
    t.GenerateRecurringExpenses({2025, 2, 28});
    CHECK(t.DeleteRecurringRule(rid));
    CHECK_EQ(t.GetExpenseCount(), 2);
    CHECK_EQ(CountForRule(t, rid), 0);
}

TEST_CASE("Subscription cost equivalents") {
    RecurringRule weekly = MakeRule("W", 10, Frequency::Weekly, {2025, 1, 1});
    CHECK_EQ(weekly.GetAnnualCost(), Money(52000));
    CHECK_EQ(weekly.GetMonthlyEquivalent(), Money(4333));
    RecurringRule yearly = MakeRule("Y", 120, Frequency::Yearly, {2025, 1, 1});
    CHECK_EQ(yearly.GetMonthlyEquivalent(), Money(1000));
}

// ── Currency (Step 2) ─────────────────────────────────────────────

TEST_CASE("Mixed currencies are normalized to the base currency") {
    ExpenseTracker t;   // base INR
    t.SetExchangeRateInBase("USD", 80.0);
    t.AddExpense(test::MakeExpense("Book", 100, {2025, 1, 1}, "Books", "USD"));
    t.AddExpense(test::MakeExpense("Chai", 5000, {2025, 1, 2}, "Food", "INR"));
    CHECK_EQ(t.GetMonthlyTotal(2025, 1), MoneyUtil::FromMajor(13000));

    t.AddExpense(test::MakeExpense("Mystery", 10, {2025, 1, 3}, "Misc", "XYZ"));
    Totals totals = t.GetTotal(2025, 1, TransactionType::Expense);
    CHECK_EQ(totals.excluded, 1);
    CHECK_EQ(totals.amount, MoneyUtil::FromMajor(13000));
    CHECK_EQ(t.CountUnconvertible(2025, 1), 1);
}

TEST_CASE("Changing the base currency converts budgets and needs no rebasing") {
    ExpenseTracker t;
    t.SetExchangeRateInBase("USD", 80.0);
    t.SetOverallBudget(2025, 1, MoneyUtil::FromMajor(8000));
    t.AddExpense(test::MakeExpense("Book", 10, {2025, 1, 1}, "Books", "USD"));
    REQUIRE(t.SetBaseCurrency("USD", true));
    CHECK_EQ(t.GetBudgetManager().GetOverallBudget(2025, 1), MoneyUtil::FromMajor(100));
    CHECK_EQ(t.GetMonthlyTotal(2025, 1), MoneyUtil::FromMajor(10));
    CHECK(std::fabs(*t.GetCurrency().GetRateInBase("INR") - 1.0 / 80.0) < 1e-12);
    CHECK(!t.SetBaseCurrency("XYZ", true));
    CHECK(!t.SetExchangeRateInBase("EUR", -1));
    CHECK(!t.SetExchangeRateInBase("EUR", 0));
}

TEST_CASE("Editing the pivot's rate keeps other currencies' base values") {
    CurrencyManager c;   // base INR
    double eurBefore = *c.GetRateInBase("EUR");
    REQUIRE(c.SetRateInBase("USD", 90.0));
    CHECK(std::fabs(*c.GetRateInBase("USD") - 90.0) < 1e-9);
    CHECK(std::fabs(*c.GetRateInBase("EUR") - eurBefore) < 1e-9);
    CHECK(!c.RemoveRate("USD"));
    CHECK(!c.RemoveRate("INR"));
}

// ── Category budgets (Step 3) ─────────────────────────────────────

TEST_CASE("Category budgets are case-insensitive and zero removes them") {
    ExpenseTracker t;
    t.SetCategoryBudget(2025, 6, "Groceries", MoneyUtil::FromMajor(300));
    CHECK_EQ(t.GetBudgetManager().GetCategoryBudget(2025, 6, "groceries"), MoneyUtil::FromMajor(300));
    t.AddExpense(test::MakeExpense("Big shop", 350, {2025, 6, 3}, "groceries"));
    CHECK_EQ(t.GetCategoryBreakdown(2025, 6)["Groceries"], MoneyUtil::FromMajor(350));
    t.SetCategoryBudget(2025, 6, "GROCERIES", 0);
    CHECK(!t.GetBudgetManager().HasCategoryBudget(2025, 6, "Groceries"));
    t.SetOverallBudget(2025, 13, 100);   // invalid month ignored
    CHECK(t.GetBudgetManager().GetOverallBudgets().empty());
}

// ── Cash flow (Step 4, G5/G6) ─────────────────────────────────────

TEST_CASE("Income and transfers never count as spending") {
    ExpenseTracker t;
    t.AddExpense(test::MakeExpense("Salary", 1000, {2025, 3, 1}, "Salary", "INR", TransactionType::Income));
    t.AddExpense(test::MakeExpense("Rent", 400, {2025, 3, 2}, "Housing"));
    t.AddExpense(test::MakeExpense("To savings", 300, {2025, 3, 3}, "Savings", "INR", TransactionType::Transfer));
    CHECK_EQ(t.GetMonthlyTotal(2025, 3), MoneyUtil::FromMajor(400));
    CHECK_EQ(t.GetMonthlyIncome(2025, 3), MoneyUtil::FromMajor(1000));
    CHECK_EQ(t.GetNetSavings(2025, 3), MoneyUtil::FromMajor(600));
    CHECK(std::fabs(*t.GetSavingsRate(2025, 3) - 60.0) < 1e-9);
    CHECK(!t.GetSavingsRate(2025, 4).has_value());
    CHECK_EQ(t.GetCategoryBreakdown(2025, 3).count("Salary"), size_t(0));
    CHECK_EQ(t.GetTopExpenses(2025, 10, 3).size(), size_t(1));
}

// ── Goals (Step 5, G7) ────────────────────────────────────────────

TEST_CASE("Goal deposits, withdrawals and required monthly deposit") {
    ExpenseTracker t;
    Goal g;
    g.title = "Laptop";
    g.targetAmount = MoneyUtil::FromMajor(1200);
    g.targetDate = {2025, 12, 31};
    int id = t.AddGoal(g);
    REQUIRE(id > 0);
    CHECK(t.DepositToGoal(id, MoneyUtil::FromMajor(200)));
    CHECK(!t.DepositToGoal(id, MoneyUtil::FromMajor(-500)));
    const Goal* goal = t.FindGoal(id);
    REQUIRE(goal != nullptr);
    CHECK_EQ(goal->MonthsRemaining({2025, 3, 15}), 10);
    CHECK_EQ(goal->GetRequiredMonthlyDeposit({2025, 3, 15}), MoneyUtil::FromMajor(100));
    CHECK_EQ(goal->MonthsRemaining({2026, 1, 1}), 0);
    CHECK_EQ(goal->GetRequiredMonthlyDeposit({2026, 1, 1}), MoneyUtil::FromMajor(1000));
    CHECK(std::fabs(goal->GetProgressPercentage() - 100.0 / 6.0) < 1e-9);

    Goal bad;
    bad.title = "No target";
    bad.targetDate = {2025, 1, 1};
    CHECK_EQ(t.AddGoal(bad), 0);
}

// ── Forecast (Step 6, G8) ─────────────────────────────────────────

TEST_CASE("Forecast excludes recurring bills from burn rate and finds the exhaust day") {
    ExpenseTracker t;
    Date today = {2025, 6, 10};
    for (int d = 1; d <= 10; d++) t.AddExpense(test::MakeExpense("Food", 100, {2025, 6, d}));
    t.AddRecurringRule(MakeRule("Internet", 500, Frequency::Monthly, {2025, 6, 5}));
    t.AddRecurringRule(MakeRule("Insurance", 300, Frequency::Monthly, {2025, 6, 20}));
    t.GenerateRecurringExpenses(today);
    t.SetOverallBudget(2025, 6, MoneyUtil::FromMajor(3000));

    ForecastResult f = t.ComputeForecast(2025, 6, today);
    CHECK(f.kind == ForecastResult::Kind::Current);
    CHECK_EQ(f.spentToDate, MoneyUtil::FromMajor(1500));
    CHECK_EQ(f.discretionaryToDate, MoneyUtil::FromMajor(1000));
    CHECK_EQ(f.dailyVelocity, MoneyUtil::FromMajor(100));
    CHECK_EQ(f.scheduledRemaining, MoneyUtil::FromMajor(300));
    CHECK_EQ(f.projectedTotal, MoneyUtil::FromMajor(3800));
    CHECK_EQ(f.exhaustDay, 22);
    CHECK(!f.alreadyExceeded);
    CHECK_EQ(f.actualCumulative.size(), size_t(10));
    CHECK_EQ(f.forecastCumulative.size(), size_t(21));

    ForecastResult past = t.ComputeForecast(2025, 5, today);
    CHECK(past.kind == ForecastResult::Kind::Past);
    CHECK(past.forecastCumulative.empty());
    ForecastResult day1 = t.ComputeForecast(2025, 7, {2025, 7, 1});
    CHECK(day1.kind == ForecastResult::Kind::Current);
    CHECK_EQ(day1.today, 1);
}

// ── Auto-categorization (Step 7, G9) ──────────────────────────────

TEST_CASE("Auto-categorizer uses word boundaries and the longest keyword") {
    AutoCategorizer a;
    CHECK_EQ(a.SuggestCategory("Uber trip to airport"), std::string("Transportation"));
    CHECK_EQ(a.SuggestCategory("UBER EATS order"), std::string("Dining"));
    CHECK_EQ(a.SuggestCategory("Weekend in Las Vegas"), std::string(""));
    CHECK_EQ(a.SuggestCategory("Swiggy dinner"), std::string("Dining"));
    CHECK_EQ(a.SuggestCategory("AT&T bill"), std::string("Utilities"));
    CHECK_EQ(a.SuggestCategory("Subscriptions"), std::string(""));   // "subway"/"sub" never partial-match
    std::string err;
    CHECK(!a.AddRule("bp", "Fuel", err));
    CHECK(a.AddRule("  Chai  Point ", "Dining", err));
    std::string kw;
    CHECK_EQ(a.SuggestCategory("chai point koramangala", &kw), std::string("Dining"));
    CHECK_EQ(kw, std::string("chai point"));
    CHECK_EQ(AutoCategorizer::SuggestKeyword("  Uber to airport"), std::string("uber"));

    auto dir = test::TempDir("rules");
    REQUIRE(a.Save(dir / "category_rules.json").ok);
    AutoCategorizer b;
    b.RemoveRule("chai point");
    REQUIRE(b.Load(dir / "category_rules.json").ok);
    CHECK_EQ(b.SuggestCategory("Chai Point"), std::string("Dining"));
}

// ── Undo / redo (Step 8, G10) ─────────────────────────────────────

TEST_CASE("Undo and redo restore exact IDs and fields") {
    ExpenseTracker t;
    CommandManager cm;
    std::string err, name;

    REQUIRE(cm.ExecuteCommand(std::make_unique<AddExpenseCommand>(t, test::MakeExpense("Lunch", 12, {2025, 1, 1})), err));
    int id = t.GetExpenses().back().GetID();
    REQUIRE(cm.Undo(name, err));
    CHECK_EQ(t.GetExpenseCount(), 0);
    REQUIRE(cm.Redo(name, err));
    REQUIRE(t.FindExpense(id) != nullptr);

    Expense edited = *t.FindExpense(id);
    edited.SetAmount(9999);
    REQUIRE(cm.ExecuteCommand(std::make_unique<UpdateExpenseCommand>(t, edited), err));
    CHECK_EQ(t.FindExpense(id)->GetAmount(), Money(9999));
    REQUIRE(cm.Undo(name, err));
    CHECK_EQ(t.FindExpense(id)->GetAmount(), Money(1200));

    Expense before = *t.FindExpense(id);
    REQUIRE(cm.ExecuteCommand(std::make_unique<DeleteExpenseCommand>(t, id), err));
    CHECK(t.FindExpense(id) == nullptr);
    REQUIRE(cm.Undo(name, err));
    CHECK_EQ(name, std::string("Delete Expense (#") + std::to_string(id) + ")");
    const Expense* restored = t.FindExpense(id);
    REQUIRE(restored != nullptr);
    CHECK_EQ(restored->GetDescription(), before.GetDescription());
    CHECK_EQ(restored->GetAmount(), before.GetAmount());
    CHECK(restored->GetDate() == before.GetDate());

    // Invalid commands are rejected and not pushed.
    CHECK(!cm.ExecuteCommand(std::make_unique<AddExpenseCommand>(t, test::MakeExpense("", 1, {2025, 1, 1})), err));
    CHECK(!err.empty());
}

TEST_CASE("Undo stack is capped and redo is cleared by new commands") {
    ExpenseTracker t;
    CommandManager cm(3);
    std::string err, name;
    for (int i = 0; i < 5; i++) {
        cm.ExecuteCommand(std::make_unique<AddExpenseCommand>(t, test::MakeExpense("x", 1, {2025, 1, 1})), err);
    }
    int undone = 0;
    while (cm.Undo(name, err)) undone++;
    CHECK_EQ(undone, 3);
    CHECK(cm.CanRedo());
    cm.ExecuteCommand(std::make_unique<AddExpenseCommand>(t, test::MakeExpense("y", 1, {2025, 1, 1})), err);
    CHECK(!cm.CanRedo());
}

TEST_CASE("Undoing the delete of a generated occurrence un-skips it") {
    ExpenseTracker t;
    CommandManager cm;
    std::string err, name;
    int rid = t.AddRecurringRule(MakeRule("Gym", 30, Frequency::Monthly, {2025, 1, 10}));
    t.GenerateRecurringExpenses({2025, 1, 31});
    int id = t.GetExpenses().back().GetID();
    cm.ExecuteCommand(std::make_unique<DeleteExpenseCommand>(t, id), err);
    CHECK_EQ(t.FindRule(rid)->GetSkippedDates().size(), size_t(1));
    cm.Undo(name, err);
    CHECK(t.FindRule(rid)->GetSkippedDates().empty());
}

// ── Reports (Step 10, G12) ────────────────────────────────────────

TEST_CASE("Reports escape user text") {
    ExpenseTracker t;
    t.AddExpense(test::MakeExpense("<script>alert(1)</script>", 50, {2025, 6, 1}, "A|B"));
    t.AddExpense(test::MakeExpense("Salary", 100, {2025, 6, 1}, "Salary", "INR", TransactionType::Income));
    std::string html = ReportGenerator::Generate(t, 2025, 6, ReportGenerator::Format::Html, {2025, 6, 30});
    CHECK(html.find("<script>") == std::string::npos);
    CHECK(html.find("&lt;script&gt;") != std::string::npos);
    CHECK(html.find("50.0%") != std::string::npos);   // savings rate
    std::string md = ReportGenerator::Generate(t, 2025, 6, ReportGenerator::Format::Markdown, {2025, 6, 30});
    CHECK(md.find("A\\|B") != std::string::npos);
    CHECK(md.find("## Top 10 Largest Transactions") != std::string::npos);
}

// ── Exchange-rate sync (parsers and merge policy; no network) ─────

#include "RateSync.h"

TEST_CASE("Rate sync parses provider responses into USD pivot rates") {
    std::string rates = R"({"USD":1,"INR":96,"EUR":0.8,"GBP":0.75,"JPY":150,"AUD":1.5,"CAD":1.4,"CHF":0.9,)"
                        R"("CNY":7,"SGD":1.3,"BAD":-1,"toolong":5})";
    RateSync::Result a;
    REQUIRE(RateSync::ParseOpenErApi(R"({"result":"success","base_code":"USD","time_last_update_unix":1790294551,"rates":)" +
                                     rates + "}", a));
    CHECK(a.ok);
    CHECK(std::fabs(a.pivotRates["INR"] - 1.0 / 96.0) < 1e-15);
    CHECK_EQ(a.pivotRates["USD"], 1.0);
    CHECK_EQ(a.pivotRates.count("BAD"), size_t(0));
    CHECK_EQ(a.asOf, std::string("2026-09-25"));

    RateSync::Result b;
    REQUIRE(RateSync::ParseFrankfurter(R"({"amount":1.0,"base":"USD","date":"2026-09-25","rates":)" + rates + "}", b));
    CHECK_EQ(b.asOf, std::string("2026-09-25"));
    CHECK(b.pivotRates.count("USD") == 1);

    RateSync::Result bad;
    CHECK(!RateSync::ParseOpenErApi("<html>moved</html>", bad));
    CHECK(!RateSync::ParseOpenErApi(R"({"result":"error","error-type":"quota"})", bad));
    CHECK(!RateSync::ParseFrankfurter(R"({"base":"EUR","rates":{}})", bad));
    CHECK(!RateSync::ParseOpenErApi(R"({"result":"success","base_code":"USD","rates":{"INR":96}})", bad));   // too few
}

TEST_CASE("Synced rates update tracked and in-use currencies only") {
    ExpenseTracker t;   // base INR, default table USD/EUR/GBP/INR/JPY/CAD/AUD
    t.AddExpense(test::MakeExpense("Dinner", 10, {2025, 1, 1}, "Food", "SGD"));   // no default rate
    t.ClearDirty();
    std::map<std::string, double> pivot = {{"USD", 1.0}, {"INR", 1.0 / 96.0}, {"SGD", 1.0 / 1.3},
                                           {"EUR", 1.0 / 0.8}, {"ZAR", 1.0 / 18.0}};
    int changed = t.ApplySyncedRates(pivot, "2026-09-25", "open.er-api.com");
    CHECK(changed >= 3);   // INR, SGD, EUR
    CHECK(t.HasChanges());
    CHECK(t.GetCurrency().HasRate("SGD"));
    CHECK(!t.GetCurrency().HasRate("ZAR"));   // not used, not tracked
    CHECK(std::fabs(*t.GetCurrency().GetRateInBase("USD") - 96.0) < 1e-9);
    CHECK_EQ(t.CountUnconvertible(2025, 1), 0);
    CHECK_EQ(t.GetCurrency().GetSource(), std::string("open.er-api.com"));

    t.ClearDirty();
    CHECK_EQ(t.ApplySyncedRates(pivot, "2026-09-25", "open.er-api.com"), 0);   // same data twice
    CHECK(!t.HasChanges());
}

// ── Restore from backup: merge, summary, cache consistency, large data ──

#include "JsonIO.h"

namespace {

LedgerData MakeBackup() {
    LedgerData d;
    d.expenses.push_back(Expense(1, "Rent", MoneyUtil::FromMajor(20000), {2024, 1, 5}, "Housing", "INR", 1));
    d.expenses.push_back(Expense(2, "Salary", MoneyUtil::FromMajor(90000), {2024, 1, 1}, "Salary", "INR", 0,
                                 TransactionType::Income));
    d.expenses.push_back(Expense(3, "Old laptop", MoneyUtil::FromMajor(800), {2023, 12, 20}, "Tech", "USD"));
    d.rules.push_back(RecurringRule(1, "Rent", MoneyUtil::FromMajor(20000), "Housing", "INR", Frequency::Monthly,
                                    {2024, 1, 5}, {2100, 12, 31}));
    d.budgets.SetOverallBudget(2024, 1, MoneyUtil::FromMajor(50000));
    Goal g;
    g.id = 1;
    g.title = "Car";
    g.targetAmount = MoneyUtil::FromMajor(500000);
    g.targetDate = {2027, 1, 1};
    d.goals.push_back(g);
    d.currency.SetPivotRate("SGD", 0.75);
    d.nextExpenseID = 4;
    d.nextRuleID = 2;
    d.nextGoalID = 2;
    return d;
}

} // namespace

TEST_CASE("Merging a backup adds missing data and skips duplicates") {
    ExpenseTracker t;
    t.AddExpense(test::MakeExpense("Groceries", 500, {2024, 1, 10}));
    t.SetOverallBudget(2024, 1, MoneyUtil::FromMajor(60000));   // must not be overwritten
    t.ClearDirty();

    LedgerData backup = MakeBackup();
    MergeResult m = t.MergeLedger(backup);
    CHECK_EQ(m.expensesAdded, 3);
    CHECK_EQ(m.rulesAdded, 1);
    CHECK_EQ(m.goalsAdded, 1);
    CHECK_EQ(m.ratesAdded, 1);
    CHECK_EQ(m.budgetsAdded, 0);
    CHECK(t.HasChanges());
    CHECK_EQ(t.GetBudgetManager().GetOverallBudget(2024, 1), MoneyUtil::FromMajor(60000));
    CHECK(t.GetCurrency().HasRate("SGD"));

    // IDs are fresh and unique; the rent bill points at the merged rule.
    std::set<int> ids;
    for (auto& e : t.GetExpenses()) CHECK(ids.insert(e.GetID()).second);
    int ruleId = t.GetRecurringRules().back().GetID();
    bool linked = false;
    for (auto& e : t.GetExpenses()) {
        if (e.GetDescription() == "Rent") linked = e.GetRecurringRuleId() == ruleId;
    }
    CHECK(linked);

    // Merging the same backup again changes nothing.
    t.ClearDirty();
    MergeResult again = t.MergeLedger(backup);
    CHECK_EQ(again.expensesAdded, 0);
    CHECK_EQ(again.expensesDuplicate, 3);
    CHECK_EQ(again.rulesDuplicate, 1);
    CHECK_EQ(again.goalsDuplicate, 1);
    CHECK(!t.HasChanges());
}

TEST_CASE("Backup summary counts types, dates and budgets") {
    LedgerSummary s = SummarizeLedger(MakeBackup());
    CHECK_EQ(s.expenses, 2);
    CHECK_EQ(s.incomes, 1);
    CHECK_EQ(s.rules, 1);
    CHECK_EQ(s.goals, 1);
    CHECK_EQ(s.budgetMonths, 1);
    CHECK(s.hasDates);
    CHECK(s.firstDate == (Date{2023, 12, 20}));
    CHECK(s.lastDate == (Date{2024, 1, 5}));
}

TEST_CASE("Cached totals always match the data after every kind of change") {
    ExpenseTracker t;
    t.AddExpense(test::MakeExpense("A", 100, {2025, 3, 1}));
    CHECK_EQ(t.GetMonthlyTotal(2025, 3), MoneyUtil::FromMajor(100));   // builds the cache
    int id = t.AddExpense(test::MakeExpense("B", 50, {2025, 3, 2}));
    CHECK_EQ(t.GetMonthlyTotal(2025, 3), MoneyUtil::FromMajor(150));
    Expense e = *t.FindExpense(id);
    e.SetAmount(MoneyUtil::FromMajor(70));
    t.UpdateExpense(e);
    CHECK_EQ(t.GetMonthlyTotal(2025, 3), MoneyUtil::FromMajor(170));
    t.DeleteExpense(id);
    CHECK_EQ(t.GetMonthlyTotal(2025, 3), MoneyUtil::FromMajor(100));
    CHECK_EQ(t.GetRecentTransactions(5).size(), size_t(1));
    t.AddExpense(test::MakeExpense("C", 10, {2025, 3, 3}, "Food", "SGD"));   // no rate yet
    CHECK_EQ(t.CountUnconvertible(2025, 3), 1);
    t.SetExchangeRateInBase("SGD", 60);
    CHECK_EQ(t.CountUnconvertible(2025, 3), 0);
    CHECK_EQ(t.GetMonthlyTotal(2025, 3), MoneyUtil::FromMajor(700));
    t.ResetAll();
    CHECK_EQ(t.GetMonthlyTotal(2025, 3), Money(0));
    CHECK(t.GetCategories().empty());
}

TEST_CASE("200,000 transactions: load, totals, merge and save stay correct and fast") {
    const int kCount = 200000;
    LedgerData big;
    big.expenses.reserve(kCount);
    Money expected = 0;
    for (int i = 0; i < kCount; i++) {
        Date d = {2015 + (i % 10), 1 + (i % 12), 1 + (i % 28)};
        Money amount = 100 + (i % 5000);
        TransactionType type = (i % 20 == 0) ? TransactionType::Income : TransactionType::Expense;
        if (type == TransactionType::Expense && d.year == 2020) expected += amount;
        big.expenses.push_back(Expense(i + 1, "Item " + std::to_string(i), amount, d,
                                       "Cat" + std::to_string(i % 15), "INR", 0, type));
    }
    big.nextExpenseID = kCount + 1;

    auto file = test::TempDir("large") / "expenses.json";
    ExpenseTracker t;
    t.ReplaceAll(std::move(big));

    auto t0 = std::chrono::steady_clock::now();
    REQUIRE(JsonIO::Save(t, file).ok);
    auto t1 = std::chrono::steady_clock::now();
    LedgerData loaded;
    JsonIO::LoadReport rep;
    REQUIRE(JsonIO::Load(file, loaded, rep).ok);
    auto t2 = std::chrono::steady_clock::now();
    CHECK_EQ(rep.loadedExpenses, kCount);
    CHECK_EQ(rep.TotalSkipped(), 0);

    ExpenseTracker u;
    u.ReplaceAll(std::move(loaded));
    CHECK_EQ(u.GetYearlyTotal(2020), expected);               // first call builds the cache
    auto t3 = std::chrono::steady_clock::now();
    for (int frame = 0; frame < 600; frame++) {               // ~10 s of frames at 60 fps
        (void)u.GetYearlyTotal(2020);
        (void)u.GetCategoryBreakdown(2020, 6);
        (void)u.GetMonthlyTotals(2020);
        (void)u.GetRecentTransactions(10);
        (void)u.GetTopExpenses(2020, 10);
    }
    auto t4 = std::chrono::steady_clock::now();

    // Merge the same data into itself: everything is a duplicate.
    LedgerData again;
    JsonIO::LoadReport rep2;
    REQUIRE(JsonIO::Load(file, again, rep2).ok);
    MergeResult m = u.MergeLedger(again);
    auto t5 = std::chrono::steady_clock::now();
    CHECK_EQ(m.expensesAdded, 0);
    CHECK_EQ(m.expensesDuplicate, kCount);

    auto ms = [](auto a, auto b) { return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count(); };
    std::cout << "    [200k] save " << ms(t0, t1) << " ms, load " << ms(t1, t2) << " ms, 600 cached frames "
              << ms(t3, t4) << " ms, merge " << ms(t4, t5) << " ms\n";
    CHECK(ms(t3, t4) < 1000);   // cached reads are near-free
    CHECK(ms(t1, t2) < 30000);  // generous bound for slow machines
}

// ── Full backup (one file = ledger + settings + category rules) ──

#include "Settings.h"

TEST_CASE("Full backup round-trips ledger, settings and category rules") {
    ExpenseTracker t;
    t.AddExpense(test::MakeExpense("Swiggy dinner", 420, {2026, 9, 1}, "Dining"));
    t.AddRecurringRule(RecurringRule(0, "Netflix", 64900, "Entertainment", "INR", Frequency::Monthly, {2026, 1, 12},
                                     {2100, 12, 31}));
    Settings s;
    s.theme = "TokyoNight";
    s.syncRatesOnLaunch = false;
    AutoCategorizer c;
    std::string err;
    REQUIRE(c.AddRule("chai point", "Dining", err));

    auto file = test::TempDir("fullbackup") / "backup.json";
    REQUIRE(JsonIO::SaveFullBackup(t, s, c, {2026, 9, 29}, file).ok);

    LedgerData data;
    JsonIO::LoadReport rep;
    JsonIO::BackupExtras extras;
    REQUIRE(JsonIO::LoadBackupFile(file, data, rep, extras).ok);
    CHECK(extras.isFullBackup);
    CHECK_EQ(extras.createdAt, std::string("2026-09-29"));
    CHECK(extras.hasSettings);
    CHECK_EQ(extras.theme, std::string("TokyoNight"));
    CHECK(!extras.syncRatesOnLaunch);
    CHECK(extras.hasRules);
    CHECK_EQ(extras.rules.count("chai point"), size_t(1));
    CHECK_EQ(data.expenses.size(), size_t(1));
    CHECK_EQ(data.rules.size(), size_t(1));

    AutoCategorizer fresh;
    fresh.ReplaceRules(extras.rules);
    CHECK_EQ(fresh.SuggestCategory("Chai Point MG road"), std::string("Dining"));
    CHECK_EQ(fresh.MergeRules(extras.rules), 0);   // already present
}

TEST_CASE("Backup loader also accepts plain ledger files") {
    LedgerData data;
    JsonIO::LoadReport rep;
    JsonIO::BackupExtras extras;
    REQUIRE(JsonIO::LoadBackupFile(test::Fixture("ledger_v1.json"), data, rep, extras).ok);
    CHECK(!extras.isFullBackup);
    CHECK(!extras.hasSettings);
    CHECK_EQ(data.expenses.size(), size_t(3));
    CHECK(!JsonIO::LoadBackupFile(test::Fixture("corrupt.json"), data, rep, extras).ok);

    auto dir = test::TempDir("badbundle");
    test::WriteText(dir / "b.json", R"({"expenseTrackerBackup": 1, "settings": {}})");
    CHECK(!JsonIO::LoadBackupFile(dir / "b.json", data, rep, extras).ok);   // no ledger inside
}

// ── Subscriptions: custom periods, renewal time, manual recording, type ──

TEST_CASE("Every-N-days subscriptions renew on the right dates and cost") {
    RecurringRule r(0, "Jio 84-day plan", MoneyUtil::FromMajor(840), "Utilities", "INR", Frequency::EveryNDays,
                    {2026, 1, 1}, {2100, 12, 31});
    r.SetIntervalDays(84);
    auto dates = r.GetOccurrences({2026, 1, 1}, {2026, 12, 31});
    REQUIRE(dates.size() == 5);
    CHECK(dates[1] == (Date{2026, 3, 26}));
    CHECK(dates[3] == (Date{2026, 9, 10}));
    CHECK(dates[4] == (Date{2026, 12, 3}));
    CHECK_EQ(r.CycleLabel(), std::string("Every 84 days"));
    CHECK_EQ(r.GetAnnualCost(), MoneyUtil::FromMajor(3650));    // 840 x 365 / 84
    std::string err;
    r.SetIntervalDays(0);
    CHECK(!r.Normalize(err));
}

TEST_CASE("Renewal time decides whether today's renewal is due") {
    ExpenseTracker t;
    RecurringRule r(0, "Netflix", MoneyUtil::FromMajor(649), "Entertainment", "INR", Frequency::Monthly,
                    {2026, 9, 28}, {2100, 12, 31});
    r.SetRenewalMinutes(9 * 60);
    int id = t.AddRecurringRule(r);
    CHECK_EQ(t.GenerateRecurringExpenses({2026, 9, 28}, 100, 8 * 60 + 59).generated, 0);   // 08:59
    CHECK_EQ(t.GenerateRecurringExpenses({2026, 9, 28}, 100, 9 * 60).generated, 1);        // 09:00
    REQUIRE(t.GetExpenseCount() == 1);
    CHECK(t.GetExpenses()[0].IsSubscription());
    CHECK_EQ(t.GetExpenses()[0].GetRecurringRuleId(), id);

    Date next;
    const RecurringRule* rule = t.FindRule(id);
    REQUIRE(rule->NextRenewal({2026, 9, 28}, 8 * 60, next));
    CHECK(next == (Date{2026, 9, 28}));                     // still later today
    REQUIRE(rule->NextRenewal({2026, 9, 28}, 10 * 60, next));
    CHECK(next == (Date{2026, 10, 28}));                    // today's already renewed
    CHECK(rule->IsDueSoon({2026, 10, 21}, 7, 0));
    CHECK(!rule->IsDueSoon({2026, 10, 20}, 7, 0));
}

TEST_CASE("Manual subscriptions are listed as due and never auto-recorded") {
    ExpenseTracker t;
    RecurringRule r(0, "Airtel", MoneyUtil::FromMajor(999), "Utilities", "INR", Frequency::Monthly,
                    {2026, 7, 10}, {2100, 12, 31});
    r.SetAutoRecord(false);
    int id = t.AddRecurringRule(r);
    CHECK_EQ(t.CountPendingOccurrences({2026, 9, 28}).total, 0);
    CHECK_EQ(t.GenerateRecurringExpenses({2026, 9, 28}).generated, 0);

    auto due = t.GetUnrecordedRenewals(*t.FindRule(id), {2026, 9, 28}, 12 * 60);
    REQUIRE(due.size() == 3);   // Jul 10, Aug 10, Sep 10
    CHECK(t.RecordSubscriptionPayment(id, due[0]) > 0);
    CHECK(t.SkipSubscriptionPayment(id, due[1]));
    due = t.GetUnrecordedRenewals(*t.FindRule(id), {2026, 9, 28}, 12 * 60);
    REQUIRE(due.size() == 1);
    CHECK(due[0] == (Date{2026, 9, 10}));
    CHECK_EQ(t.GetExpenseCount(), 1);
    CHECK(t.GetExpenses()[0].IsSubscription());
}

TEST_CASE("Subscription payments count as spending everywhere") {
    ExpenseTracker t;
    t.AddExpense(test::MakeExpense("Groceries", 1000, {2026, 9, 5}, "Food"));
    t.AddExpense(test::MakeExpense("Netflix", 649, {2026, 9, 12}, "Entertainment", "INR", TransactionType::Subscription));
    t.AddExpense(test::MakeExpense("Salary", 10000, {2026, 9, 1}, "Salary", "INR", TransactionType::Income));
    CHECK_EQ(t.GetMonthlyTotal(2026, 9), MoneyUtil::FromMajor(1649));
    CHECK_EQ(t.GetTotal(2026, 9, TransactionType::Subscription).amount, MoneyUtil::FromMajor(649));
    CHECK_EQ(t.GetCategoryBreakdown(2026, 9)["Entertainment"], MoneyUtil::FromMajor(649));
    CHECK_EQ(t.GetTopExpenses(2026, 10, 9).size(), size_t(2));
    CHECK(std::fabs(*t.GetSavingsRate(2026, 9) - 83.51) < 0.01);
}

TEST_CASE("Old generated bills load as subscriptions; new rule fields round-trip") {
    LedgerData d;
    RecurringRule rule(1, "Gym", MoneyUtil::FromMajor(1500), "Health", "INR", Frequency::EveryNDays, {2026, 1, 3},
                       {2100, 12, 31});
    rule.SetIntervalDays(24);
    rule.SetRenewalMinutes(18 * 60 + 30);
    rule.SetAutoRecord(false);
    d.rules.push_back(rule);
    d.expenses.push_back(Expense(1, "Gym", MoneyUtil::FromMajor(1500), {2026, 1, 3}, "Health", "INR", 1));   // old: plain expense
    d.nextExpenseID = 2;
    d.nextRuleID = 2;
    ExpenseTracker t;
    t.ReplaceAll(std::move(d));
    CHECK(t.GetExpenses()[0].IsSubscription());

    auto file = test::TempDir("subsfields") / "expenses.json";
    REQUIRE(JsonIO::Save(t, file).ok);
    LedgerData back;
    JsonIO::LoadReport rep;
    REQUIRE(JsonIO::Load(file, back, rep).ok);
    REQUIRE(back.rules.size() == 1);
    CHECK(back.rules[0].GetFrequency() == Frequency::EveryNDays);
    CHECK_EQ(back.rules[0].GetIntervalDays(), 24);
    CHECK_EQ(back.rules[0].TimeString(), std::string("18:30"));
    CHECK(!back.rules[0].IsAutoRecord());
    CHECK(back.expenses[0].IsSubscription());
}
