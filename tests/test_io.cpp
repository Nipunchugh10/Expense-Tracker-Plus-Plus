// Persistence, import/export and file safety (Phase 0 group A).
#include "TestFramework.h"
#include "TestHelpers.h"
#include "CsvIO.h"
#include "JsonIO.h"
#include "Paths.h"
#include "Utils.h"
#include <cmath>
#include <set>

namespace fs = std::filesystem;

namespace {

bool LoadInto(ExpenseTracker& t, const fs::path& p, JsonIO::LoadReport& rep) {
    LedgerData data;
    IoResult r = JsonIO::Load(p, data, rep);
    if (r.ok) t.ReplaceAll(std::move(data));
    return r.ok;
}

bool UniqueIds(const ExpenseTracker& t) {
    std::set<int> ids;
    for (auto& e : t.GetExpenses()) {
        if (!ids.insert(e.GetID()).second) return false;
    }
    return true;
}

} // namespace

TEST_CASE("Version-1 ledger loads with budgets, types and IDs intact") {
    ExpenseTracker t;
    JsonIO::LoadReport rep;
    REQUIRE(LoadInto(t, test::Fixture("ledger_v1.json"), rep));
    CHECK_EQ(rep.fileVersion, 1);
    CHECK(!rep.newerVersion);
    CHECK_EQ(t.GetExpenseCount(), 3);
    CHECK_EQ(rep.TotalSkipped(), 0);
    for (auto& e : t.GetExpenses()) CHECK(e.IsExpense());
    CHECK_EQ(t.GetBudgetManager().GetOverallBudget(2025, 3), Money(500000));
    CHECK_EQ(t.GetBudgetManager().GetOverallBudget(2025, 4), Money(400000));
    CHECK_EQ(t.GetNextExpenseID(), 4);
    CHECK_EQ(t.GetCategories().size(), size_t(2));   // "Food" and "food " merged
    CHECK(!t.HasChanges());
}

TEST_CASE("Hostile JSON loads without crashing and reports what was skipped") {
    ExpenseTracker t;
    JsonIO::LoadReport rep;
    REQUIRE(LoadInto(t, test::Fixture("ledger_hostile.json"), rep));
    CHECK_EQ(rep.loadedExpenses, 4);
    CHECK_EQ(rep.skippedExpenses, 8);
    CHECK_EQ(rep.loadedRules, 1);
    CHECK_EQ(rep.skippedRules, 1);
    CHECK_EQ(rep.loadedGoals, 1);
    CHECK_EQ(rep.skippedGoals, 1);
    CHECK_EQ(rep.skippedBudgets, 5);
    CHECK(rep.reassignedIds >= 2);
    CHECK(UniqueIds(t));
    CHECK(t.GetNextExpenseID() > 4);
    CHECK_EQ(t.GetBudgetManager().GetOverallBudget(2025, 2), Money(25000));
    CHECK(!rep.messages.empty());

    // A new expense never collides with loaded IDs.
    int id = t.AddExpense(test::MakeExpense("New", 1, Date{2025, 1, 5}));
    CHECK(id > 0);
    CHECK(UniqueIds(t));
}

TEST_CASE("Corrupt, empty and non-object files fail cleanly") {
    LedgerData data;
    JsonIO::LoadReport rep;
    CHECK(!JsonIO::Load(test::Fixture("corrupt.json"), data, rep).ok);
    fs::path dir = test::TempDir("corrupt");
    test::WriteText(dir / "empty.json", "   \n");
    CHECK(!JsonIO::Load(dir / "empty.json", data, rep).ok);
    test::WriteText(dir / "array.json", "[1,2,3]");
    CHECK(!JsonIO::Load(dir / "array.json", data, rep).ok);
    CHECK(!JsonIO::Load(dir / "missing.json", data, rep).ok);
}

TEST_CASE("A ledger from a newer version is flagged read-only") {
    fs::path dir = test::TempDir("newer");
    test::WriteText(dir / "expenses.json", R"({"version": 99, "expenses": []})");
    LedgerData data;
    JsonIO::LoadReport rep;
    REQUIRE(JsonIO::Load(dir / "expenses.json", data, rep).ok);
    CHECK(rep.newerVersion);
}

TEST_CASE("Save/load round-trips every entity and Unicode text byte-identically") {
    ExpenseTracker t;
    std::string hindi = "\xE0\xA4\xA8\xE0\xA4\xAE\xE0\xA4\xB8\xE0\xA5\x8D\xE0\xA4\xA4\xE0\xA5\x87 chai";
    std::string longDesc(1000, 'L');
    t.AddExpense(test::MakeExpense(hindi, 12345678.90, Date{2025, 7, 4}));
    t.AddExpense(test::MakeExpense(longDesc, 1, Date{2025, 7, 5}));
    t.AddExpense(test::MakeExpense("Salary", 90000, Date{2025, 7, 1}, "Salary", "INR", TransactionType::Income));
    RecurringRule r(0, "Netflix", 64900, "Entertainment", "INR", Frequency::Monthly, Date{2025, 1, 31}, Date{2030, 1, 1});
    int rid = t.AddRecurringRule(r);
    t.GenerateRecurringExpenses(Date{2025, 3, 31});
    t.DeleteExpense(t.GetExpenses().back().GetID());   // creates a skipped date
    t.SetCategoryBudget(2025, 7, "Food", 50000);
    t.SetOverallBudget(2025, 7, 250000);
    Goal g;
    g.title = "Emergency Fund";
    g.targetAmount = 1000000;
    g.currentAmount = 250000;
    g.targetDate = {2030, 1, 1};
    t.AddGoal(g);
    t.SetExchangeRateInBase("USD", 84.0);

    fs::path file = test::TempDir("roundtrip") / "expenses.json";
    REQUIRE(JsonIO::Save(t, file).ok);

    ExpenseTracker u;
    JsonIO::LoadReport rep;
    REQUIRE(LoadInto(u, file, rep));
    CHECK_EQ(rep.TotalSkipped(), 0);
    CHECK_EQ(u.GetExpenseCount(), t.GetExpenseCount());
    CHECK_EQ(u.GetExpenses()[0].GetDescription(), hindi);
    CHECK_EQ(u.GetExpenses()[0].GetAmount(), Money(1234567890));
    CHECK_EQ(u.GetExpenses()[1].GetDescription(), longDesc);
    CHECK(u.GetExpenses()[2].IsIncome());
    const RecurringRule* lr = u.FindRule(rid);
    REQUIRE(lr != nullptr);
    CHECK(lr->HasGenerated());
    CHECK_EQ(lr->GetSkippedDates().size(), size_t(1));
    CHECK_EQ(u.GetBudgetManager().GetCategoryBudget(2025, 7, "food"), Money(50000));
    CHECK_EQ(u.GetBudgetManager().GetOverallBudget(2025, 7), Money(250000));
    REQUIRE(u.GetGoals().size() == 1);
    CHECK_EQ(u.GetGoals()[0].currentAmount, Money(250000));
    CHECK(std::fabs(*u.GetCurrency().GetRateInBase("USD") - 84.0) < 1e-9);
    CHECK_EQ(u.GetNextExpenseID(), t.GetNextExpenseID());
    CHECK_EQ(u.GetNextRuleID(), t.GetNextRuleID());

    std::string json = test::ReadText(file);
    CHECK(json.find("\"version\": 2") != std::string::npos);
    CHECK(json.find("\"budgets\"") != std::string::npos);   // legacy mirror for older builds
}

TEST_CASE("Invalid UTF-8 never makes saving throw") {
    LedgerData data;
    data.expenses.push_back(Expense(1, "bad \xFF\xFE bytes", 100, Date{2025, 1, 1}, "Misc", "INR"));
    data.nextExpenseID = 2;
    ExpenseTracker t;
    t.ReplaceAll(std::move(data));
    fs::path file = test::TempDir("utf8") / "expenses.json";
    IoResult r = JsonIO::Save(t, file);
    CHECK(r.ok);
    CHECK(Utils::IsValidUtf8(test::ReadText(file)));
}

TEST_CASE("Atomic save keeps the previous file when interrupted and rotates a backup") {
    fs::path dir = test::TempDir("atomic");
    fs::path file = dir / "expenses.json";
    REQUIRE(WriteFileAtomic(file, "first").ok);
    REQUIRE(WriteFileAtomic(file, "second").ok);
    CHECK_EQ(test::ReadText(file), std::string("second"));
    CHECK_EQ(test::ReadText(dir / "expenses.json.bak"), std::string("first"));

    AtomicWriteOptions crash;
    crash.simulateFailureAfterTemp = true;
    CHECK(!WriteFileAtomic(file, "third", crash).ok);
    CHECK_EQ(test::ReadText(file), std::string("second"));
}

TEST_CASE("Writing into a read-only location fails with a message instead of throwing") {
    IoResult r = WriteFileAtomic(fs::path("Z:\\definitely\\missing\\drive\\x.json"), "data");
    CHECK(!r.ok);
    CHECK(!r.message.empty());
}

TEST_CASE("CSV import persists, re-IDs and reports duplicates") {
    ExpenseTracker t;
    t.AddExpense(test::MakeExpense("Existing", 5, Date{2025, 5, 1}));
    t.ClearDirty();

    bool cp = false;
    std::string content = CsvIO::DecodeText(test::ReadText(test::Fixture("quoted_newlines.csv")), cp);
    CHECK(!CsvIO::IsLegacyFormat(content));
    auto parsed = CsvIO::Parse(content, 2025);
    REQUIRE(parsed.drafts.size() == 2);
    CHECK_EQ(parsed.skipped, 1);   // negative refund row
    CHECK_EQ(parsed.drafts[0].GetDescription(), std::string("Line one line two, with \"quotes\""));
    CHECK(parsed.drafts[1].IsIncome());
    CHECK_EQ(parsed.drafts[1].GetAmount(), Money(5000000));

    auto result = t.ImportExpenses(parsed.drafts, false);
    CHECK_EQ(result.imported, 2);
    CHECK(t.HasChanges());   // import is persisted by autosave/shutdown (P0-A2)
    CHECK(UniqueIds(t));
    for (auto& e : t.GetExpenses()) CHECK(e.GetID() != 7 && e.GetID() != 8);

    // Importing the same rows again is detected as duplicates.
    auto dups = t.FindLikelyDuplicates(parsed.drafts);
    CHECK_EQ(dups.size(), size_t(2));
    auto again = t.ImportExpenses(parsed.drafts, true);
    CHECK_EQ(again.imported, 0);
    CHECK_EQ(again.duplicatesSkipped, 2);

    // Survives a save/load cycle.
    fs::path file = test::TempDir("import") / "expenses.json";
    REQUIRE(JsonIO::Save(t, file).ok);
    ExpenseTracker u;
    JsonIO::LoadReport rep;
    REQUIRE(LoadInto(u, file, rep));
    CHECK_EQ(u.GetExpenseCount(), 3);
}

TEST_CASE("Legacy CSV uses the chosen year and skips invalid months") {
    bool cp = false;
    std::string content = CsvIO::DecodeText(test::ReadText(test::Fixture("legacy.csv")), cp);
    CHECK(CsvIO::IsLegacyFormat(content));
    auto parsed = CsvIO::Parse(content, 2023);
    REQUIRE(parsed.drafts.size() == 2);
    CHECK_EQ(parsed.skipped, 1);
    CHECK(parsed.drafts[0].GetDate() == (Date{2023, 2, 1}));
    CHECK_EQ(parsed.drafts[0].GetAmount(), Money(350));
}

TEST_CASE("Windows-1252 CSV is converted to UTF-8") {
    std::string raw = "ID,Date,Description,Amount,Currency,Category\n1,2025-01-01,Caf\xE9 \x80 tip,4.50,EUR,Dining\n";
    bool cp = false;
    std::string content = CsvIO::DecodeText(raw, cp);
    CHECK(cp);
    auto parsed = CsvIO::Parse(content, 2025);
    REQUIRE(parsed.drafts.size() == 1);
    CHECK_EQ(parsed.drafts[0].GetDescription(), std::string("Caf\xC3\xA9 \xE2\x82\xAC tip"));
    ExpenseTracker t;
    t.ImportExpenses(parsed.drafts, false);
    fs::path file = test::TempDir("cp1252") / "expenses.json";
    CHECK(JsonIO::Save(t, file).ok);
}

TEST_CASE("CSV export round-trips large amounts and protects formulas") {
    ExpenseTracker t;
    t.AddExpense(test::MakeExpense("=HYPERLINK(\"http://x\",\"y\")", 12345678.90, Date{2025, 1, 1}, "@risk"));
    t.AddExpense(test::MakeExpense("Multi\nline", 0.01, Date{2025, 1, 2}));
    std::string csv = CsvIO::ExportToString(t);
    CHECK(csv.find("12345678.90") != std::string::npos);
    CHECK(csv.find("e+") == std::string::npos);
    CHECK(csv.find("\"'=HYPERLINK(\"\"http://x\"\",\"\"y\"\")\"") != std::string::npos);
    CHECK(csv.find("'@risk") != std::string::npos);

    auto parsed = CsvIO::Parse(csv, 2025);
    REQUIRE(parsed.drafts.size() == 2);
    CHECK_EQ(parsed.drafts[0].GetAmount(), Money(1234567890));
    CHECK_EQ(parsed.drafts[0].GetDescription(), std::string("=HYPERLINK(\"http://x\",\"y\")"));
    CHECK_EQ(parsed.drafts[0].GetCategory(), std::string("@risk"));
}

TEST_CASE("Paths build timestamped siblings and honour the test data dir") {
    fs::path dir = test::TempDir("paths");
    Paths::SetDataDirForTesting(dir);
    CHECK(Paths::LedgerFile() == dir / "expenses.json");
    fs::path corrupt = Paths::TimestampedSibling(Paths::LedgerFile(), "corrupt");
    std::string name = Paths::ToUtf8(corrupt.filename());
    CHECK(name.rfind("expenses.corrupt-", 0) == 0);
    CHECK(name.size() == std::string("expenses.corrupt-20250101-120000.json").size());

    fs::path unicodeDir = dir / Paths::FromUtf8("\xE0\xA4\xA8\xE0\xA4\xAE\xE0\xA4\xB8\xE0\xA5\x8D\xE0\xA4\xA4\xE0\xA5\x87");
    ExpenseTracker t;
    t.AddExpense(test::MakeExpense("x", 1, Date{2025, 1, 1}));
    CHECK(CsvIO::Export(t, unicodeDir / "out.csv").ok);
    CHECK(JsonIO::Save(t, unicodeDir / "expenses.json").ok);
    LedgerData data;
    JsonIO::LoadReport rep;
    CHECK(JsonIO::Load(unicodeDir / "expenses.json", data, rep).ok);
}
