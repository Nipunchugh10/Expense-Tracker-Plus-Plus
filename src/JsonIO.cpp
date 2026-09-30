#include "JsonIO.h"
#include "ExpenseTracker.h"
#include "Utils.h"
#include "AutoCategorizer.h"
#include "Settings.h"
#include "Validation.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <set>

using json = nlohmann::json;

namespace JsonIO {

namespace {

// ── Typed field readers: return false when the key is missing or has the wrong type ──

bool GetString(const json& o, const char* key, std::string& out) {
    auto it = o.find(key);
    if (it == o.end() || !it->is_string()) return false;
    out = it->get<std::string>();
    return true;
}

bool GetNumber(const json& o, const char* key, double& out) {
    auto it = o.find(key);
    if (it == o.end() || !it->is_number()) return false;
    out = it->get<double>();
    return true;
}

bool GetInt(const json& o, const char* key, long long& out) {
    auto it = o.find(key);
    if (it == o.end()) return false;
    if (it->is_number_integer()) {
        out = it->get<long long>();
        return true;
    }
    if (it->is_number_float()) {
        double d = it->get<double>();
        if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 1e15) {
            out = static_cast<long long>(d);
            return true;
        }
    }
    return false;
}

bool GetBool(const json& o, const char* key, bool& out) {
    auto it = o.find(key);
    if (it == o.end() || !it->is_boolean()) return false;
    out = it->get<bool>();
    return true;
}

bool GetMoney(const json& o, const char* key, Money& out, std::string& why) {
    double v;
    if (!GetNumber(o, key, v)) {
        why = std::string("'") + key + "' is missing or not a number";
        return false;
    }
    return Validation::ValidateAmount(v, out, why);
}

bool GetDate(const json& o, const char* key, Date& out) {
    std::string s;
    return GetString(o, key, s) && Date::TryParse(s, out);
}

int ClampId(long long v) {
    return (v > 0 && v < INT_MAX) ? static_cast<int>(v) : 0;
}

// "YYYYMM" (overall) or "YYYY-MM" (category) month keys.
bool ParseMonthKey(const std::string& key, bool dashed, int& year, int& month) {
    std::string digits = key;
    if (dashed) {
        if (key.size() != 7 || key[4] != '-') return false;
        digits = key.substr(0, 4) + key.substr(5, 2);
    }
    if (digits.size() != 6) return false;
    for (char c : digits) {
        if (c < '0' || c > '9') return false;
    }
    year = std::stoi(digits.substr(0, 4));
    month = std::stoi(digits.substr(4, 2));
    return month >= 1 && month <= 12 && year == Validation::ClampYear(year);
}

std::string MonthKeyDashed(int key) {
    int y, m;
    BudgetManager::SplitKey(key, y, m);
    std::string mm = std::to_string(m);
    return std::to_string(y) + "-" + (mm.size() < 2 ? "0" + mm : mm);
}

bool ParseType(const json& ej, TransactionType& type) {
    auto it = ej.find("type");
    type = TransactionType::Expense;
    if (it == ej.end() || it->is_null()) return true;
    if (it->is_string()) return TransactionTypeFromString(it->get<std::string>(), type);
    if (it->is_number_integer()) {
        long long v = it->get<long long>();
        if (v >= 0 && v < kTransactionTypeCount) {
            type = static_cast<TransactionType>(v);
            return true;
        }
    }
    return false;
}

bool ParseExpense(const json& ej, Expense& out, std::string& why, bool& typeWarning) {
    if (!ej.is_object()) { why = "not a JSON object"; return false; }
    long long id = 0;
    GetInt(ej, "id", id);
    std::string desc;
    if (!GetString(ej, "description", desc)) { why = "'description' is missing or not text"; return false; }
    Money amount;
    if (!GetMoney(ej, "amount", amount, why)) return false;
    Date date;
    if (!GetDate(ej, "date", date)) { why = "'date' is missing or not a valid YYYY-MM-DD date"; return false; }
    std::string category, currency = kDefaultCurrency;
    GetString(ej, "category", category);
    GetString(ej, "currency", currency);
    long long rid = 0;
    GetInt(ej, "recurringRuleId", rid);
    TransactionType type;
    typeWarning = !ParseType(ej, type);

    out = Expense(ClampId(id), desc, amount, date, category, currency, ClampId(rid), type);
    return Validation::NormalizeExpense(out, why);
}

bool ParseRule(const json& rj, RecurringRule& out, std::string& why) {
    if (!rj.is_object()) { why = "not a JSON object"; return false; }
    long long id = 0;
    GetInt(rj, "id", id);
    RecurringRule r;
    r.SetID(ClampId(id));

    std::string s;
    if (!GetString(rj, "description", s)) { why = "'description' is missing or not text"; return false; }
    r.SetDescription(s);
    Money amount;
    if (!GetMoney(rj, "amount", amount, why)) return false;
    r.SetAmount(amount);
    if (GetString(rj, "category", s)) r.SetCategory(s);
    if (GetString(rj, "currency", s)) r.SetCurrency(s);
    if (GetString(rj, "note", s)) r.SetNote(s);

    Frequency f = Frequency::Monthly;
    if (GetString(rj, "frequency", s) && !RecurringRule::FrequencyFromString(s, f)) {
        why = "unknown frequency '" + s + "'";
        return false;
    }
    r.SetFrequency(f);
    long long interval = 30;
    if (GetInt(rj, "intervalDays", interval)) r.SetIntervalDays(static_cast<int>(std::clamp<long long>(interval, 0, 100000)));
    std::string time;
    if (GetString(rj, "time", time)) {
        int hh = -1, mm = -1;
        if (time.size() == 5 && time[2] == ':' && std::isdigit(static_cast<unsigned char>(time[0])) &&
            std::isdigit(static_cast<unsigned char>(time[1])) && std::isdigit(static_cast<unsigned char>(time[3])) &&
            std::isdigit(static_cast<unsigned char>(time[4]))) {
            hh = (time[0] - '0') * 10 + (time[1] - '0');
            mm = (time[3] - '0') * 10 + (time[4] - '0');
        }
        if (hh < 0 || hh > 23 || mm < 0 || mm > 59) { why = "'time' must be HH:MM"; return false; }
        r.SetRenewalMinutes(hh * 60 + mm);
    }
    bool autoRecord = true;
    GetBool(rj, "autoRecord", autoRecord);
    r.SetAutoRecord(autoRecord);

    Date start;
    if (!GetDate(rj, "startDate", start)) { why = "'startDate' is missing or invalid"; return false; }
    r.SetStartDate(start);
    Date end = {Validation::kMaxYear, 12, 31};
    if (rj.contains("endDate") && !GetDate(rj, "endDate", end)) { why = "'endDate' is invalid"; return false; }
    r.SetEndDate(end);

    bool active = true;
    GetBool(rj, "active", active);
    r.SetActive(active);

    Date last;
    if (GetDate(rj, "lastGeneratedThrough", last)) r.SetLastGeneratedThrough(last);
    auto skipped = rj.find("skippedDates");
    if (skipped != rj.end() && skipped->is_array()) {
        for (auto& sd : *skipped) {
            Date d;
            if (sd.is_string() && Date::TryParse(sd.get<std::string>(), d)) r.AddSkippedDate(d);
        }
    }
    if (!r.Normalize(why)) return false;
    out = r;
    return true;
}

bool ParseGoal(const json& gj, Goal& out, std::string& why) {
    if (!gj.is_object()) { why = "not a JSON object"; return false; }
    Goal g;
    long long id = 0;
    GetInt(gj, "id", id);
    g.id = ClampId(id);
    if (!GetString(gj, "title", g.title)) { why = "'title' is missing or not text"; return false; }
    if (!GetMoney(gj, "targetAmount", g.targetAmount, why)) return false;
    if (gj.contains("currentAmount") && !GetMoney(gj, "currentAmount", g.currentAmount, why)) return false;
    if (!GetDate(gj, "targetDate", g.targetDate)) { why = "'targetDate' is missing or invalid"; return false; }
    GetString(gj, "currency", g.currency);
    GetString(gj, "color", g.color);
    if (!g.Normalize(why)) return false;
    out = g;
    return true;
}

// Assigns fresh IDs to entries with a missing or duplicate ID and returns
// the next free ID (max(fileNext, maxId + 1)).
template <class T, class GetId, class SetId>
int FixIds(std::vector<T>& items, long long fileNext, GetId getId, SetId setId, int& reassigned) {
    std::set<int> used;
    int maxId = 0;
    std::vector<size_t> needId;
    for (size_t i = 0; i < items.size(); i++) {
        int id = getId(items[i]);
        if (id <= 0 || !used.insert(id).second) needId.push_back(i);
        else maxId = std::max(maxId, id);
    }
    for (size_t idx : needId) {
        setId(items[idx], ++maxId);
        reassigned++;
    }
    long long next = std::max<long long>(fileNext, static_cast<long long>(maxId) + 1);
    return static_cast<int>(std::min<long long>(next, INT_MAX - 1));
}

} // namespace

// ── Save ──────────────────────────────────────────────────────────

static json BuildLedgerJson(const ExpenseTracker& t) {
    json j;
    j["version"] = kSchemaVersion;
    j["baseCurrency"] = t.GetBaseCurrency();

    json rates = json::object();
    for (auto& [code, v] : t.GetCurrency().GetAllRates()) rates[code] = v;
    j["exchangeRates"] = {{"pivot", CurrencyManager::kPivot}, {"asOf", t.GetCurrency().GetAsOf()},
                        {"source", t.GetCurrency().GetSource()}, {"rates", rates}};

    j["nextExpenseID"] = t.GetNextExpenseID();
    j["nextRuleID"] = t.GetNextRuleID();
    j["nextGoalID"] = t.GetNextGoalID();

    json expArr = json::array();
    for (auto& e : t.GetExpenses()) {
        expArr.push_back({
            {"id", e.GetID()},
            {"type", TransactionTypeToString(e.GetType())},
            {"description", e.GetDescription()},
            {"amount", MoneyUtil::ToMajor(e.GetAmount())},
            {"date", e.GetDate().ToString()},
            {"category", e.GetCategory()},
            {"currency", e.GetCurrency()},
            {"recurringRuleId", e.GetRecurringRuleId()},
        });
    }
    j["expenses"] = expArr;

    json ruleArr = json::array();
    for (auto& r : t.GetRecurringRules()) {
        json rj = {
            {"id", r.GetID()},
            {"description", r.GetDescription()},
            {"amount", MoneyUtil::ToMajor(r.GetAmount())},
            {"category", r.GetCategory()},
            {"currency", r.GetCurrency()},
            {"frequency", RecurringRule::FrequencyToString(r.GetFrequency())},
            {"intervalDays", r.GetIntervalDays()},
            {"time", r.TimeString()},
            {"autoRecord", r.IsAutoRecord()},
            {"startDate", r.GetStartDate().ToString()},
            {"endDate", r.GetEndDate().ToString()},
            {"active", r.IsActive()},
            {"note", r.GetNote()},
        };
        if (r.HasGenerated()) rj["lastGeneratedThrough"] = r.GetLastGeneratedThrough().ToString();
        json skipped = json::array();
        for (int d : r.GetSkippedDates()) skipped.push_back(Date::FromInt(d).ToString());
        rj["skippedDates"] = skipped;
        ruleArr.push_back(rj);
    }
    j["recurringRules"] = ruleArr;

    json overall = json::object();
    for (auto& [key, limit] : t.GetBudgetManager().GetOverallBudgets()) {
        overall[std::to_string(key)] = MoneyUtil::ToMajor(limit);
    }
    j["overallBudgets"] = overall;
    j["budgets"] = overall;   // legacy mirror so older builds still see monthly budgets (G4)

    json catBudgets = json::object();
    for (auto& [key, month] : t.GetBudgetManager().GetAllCategoryBudgets()) {
        json mj = json::object();
        for (auto& [lower, entry] : month) mj[entry.displayName] = MoneyUtil::ToMajor(entry.limit);
        catBudgets[MonthKeyDashed(key)] = mj;
    }
    j["categoryBudgets"] = catBudgets;

    json goalArr = json::array();
    for (auto& g : t.GetGoals()) {
        goalArr.push_back({
            {"id", g.id},
            {"title", g.title},
            {"targetAmount", MoneyUtil::ToMajor(g.targetAmount)},
            {"currentAmount", MoneyUtil::ToMajor(g.currentAmount)},
            {"targetDate", g.targetDate.ToString()},
            {"currency", g.currency},
            {"color", g.color},
        });
    }
    j["goals"] = goalArr;

    return j;
}

std::string Serialize(const ExpenseTracker& t) {
    // Invalid UTF-8 is replaced instead of throwing (last line of defence, P0-A5).
    return BuildLedgerJson(t).dump(2, ' ', false, json::error_handler_t::replace);
}

IoResult Save(const ExpenseTracker& tracker, const std::filesystem::path& filepath) {
    try {
        return WriteFileAtomic(filepath, Serialize(tracker));
    } catch (const std::exception& e) {
        return IoResult::Fail(std::string("Saving failed: ") + e.what());
    }
}

// ── Load ──────────────────────────────────────────────────────────

static bool ParseLedgerJson(const json& j, LedgerData& out, LoadReport& rep, std::string& error) {
    try {
        if (!j.is_object()) {
            error = "The file is not a ledger (its top level is not a JSON object).";
            return false;
        }
        LedgerData data;

        // Schema version (missing = 1)
        long long version = 1;
        if (j.contains("version") && (!GetInt(j, "version", version) || version < 1)) {
            rep.messages.push_back("Unrecognised 'version' value; treated as version 1.");
            version = 1;
        }
        rep.fileVersion = static_cast<int>(std::min<long long>(version, 1000000));
        rep.newerVersion = version > kSchemaVersion;

        // Currency (single source of truth in the ledger, G1)
        std::string base = kDefaultCurrency, s;
        if (GetString(j, "baseCurrency", s) && !Validation::NormalizeCurrency(s, base)) {
            rep.messages.push_back("Invalid base currency '" + s + "'; using " + kDefaultCurrency + ".");
            base = kDefaultCurrency;
        }
        auto er = j.find("exchangeRates");
        if (er != j.end() && er->is_object() && er->contains("rates") && (*er)["rates"].is_object()) {
            std::map<std::string, double> rates;
            for (auto& [code, v] : (*er)["rates"].items()) {
                std::string c;
                if (Validation::NormalizeCurrency(code, c) && v.is_number() &&
                    std::isfinite(v.get<double>()) && v.get<double>() > 0) {
                    rates[c] = v.get<double>();
                } else {
                    rep.messages.push_back("Ignored invalid exchange rate entry '" + code + "'.");
                }
            }
            std::string asOf;
            GetString(*er, "asOf", asOf);
            data.currency.Assign(base, rates, asOf);
            std::string source;
            GetString(*er, "source", source);
            data.currency.SetSource(source);
        } else {
            data.currency.ResetToDefaults();
            data.currency.SetBaseCurrency(base);
        }
        if (data.currency.GetBaseCurrency() != base) {
            rep.messages.push_back("No exchange rate for base currency " + base + "; using " +
                                   data.currency.GetBaseCurrency() + ".");
        }

        // Expenses
        auto ex = j.find("expenses");
        if (ex != j.end() && !ex->is_array()) rep.messages.push_back("'expenses' is not a list and was ignored.");
        if (ex != j.end() && ex->is_array()) {
            for (size_t i = 0; i < ex->size(); i++) {
                Expense e;
                std::string why;
                bool typeWarning = false;
                if (!ParseExpense((*ex)[i], e, why, typeWarning)) {
                    rep.skippedExpenses++;
                    rep.messages.push_back("Transaction " + std::to_string(i + 1) + " skipped: " + why + ".");
                    continue;
                }
                if (typeWarning) {
                    rep.messages.push_back("Transaction " + std::to_string(i + 1) +
                                           " has an unknown type; treated as an expense.");
                }
                data.expenses.push_back(e);
            }
        }

        // Recurring rules
        auto rr = j.find("recurringRules");
        if (rr != j.end() && rr->is_array()) {
            for (size_t i = 0; i < rr->size(); i++) {
                RecurringRule r;
                std::string why;
                if (!ParseRule((*rr)[i], r, why)) {
                    rep.skippedRules++;
                    rep.messages.push_back("Subscription " + std::to_string(i + 1) + " skipped: " + why + ".");
                    continue;
                }
                data.rules.push_back(r);
            }
        }

        // Goals
        auto gl = j.find("goals");
        if (gl != j.end() && gl->is_array()) {
            for (size_t i = 0; i < gl->size(); i++) {
                Goal g;
                std::string why;
                if (!ParseGoal((*gl)[i], g, why)) {
                    rep.skippedGoals++;
                    rep.messages.push_back("Goal " + std::to_string(i + 1) + " skipped: " + why + ".");
                    continue;
                }
                data.goals.push_back(g);
            }
        }

        // IDs: unique, positive, and next IDs above every existing ID (P0-A3)
        long long fileNext = 1;
        GetInt(j, "nextExpenseID", fileNext);
        data.nextExpenseID = FixIds(data.expenses, fileNext,
            [](const Expense& e) { return e.GetID(); }, [](Expense& e, int id) { e.SetID(id); }, rep.reassignedIds);
        fileNext = 1;
        GetInt(j, "nextRuleID", fileNext);
        data.nextRuleID = FixIds(data.rules, fileNext,
            [](const RecurringRule& r) { return r.GetID(); }, [](RecurringRule& r, int id) { r.SetID(id); }, rep.reassignedIds);
        fileNext = 1;
        GetInt(j, "nextGoalID", fileNext);
        data.nextGoalID = FixIds(data.goals, fileNext,
            [](const Goal& g) { return g.id; }, [](Goal& g, int id) { g.id = id; }, rep.reassignedIds);
        if (rep.reassignedIds > 0) {
            rep.messages.push_back(std::to_string(rep.reassignedIds) + " missing or duplicate IDs were reassigned.");
        }

        // Expenses may only link to rules that exist.
        std::set<int> ruleIds;
        for (auto& r : data.rules) ruleIds.insert(r.GetID());
        for (auto& e : data.expenses) {
            if (e.GetRecurringRuleId() != 0 && !ruleIds.count(e.GetRecurringRuleId())) e.SetRecurringRuleId(0);
        }

        // Budgets: "overallBudgets" (v2) or legacy "budgets" (v1) (G4)
        const char* overallKey = j.contains("overallBudgets") ? "overallBudgets" : "budgets";
        auto ob = j.find(overallKey);
        if (ob != j.end() && ob->is_object()) {
            for (auto& [key, v] : ob->items()) {
                int y, m;
                Money limit;
                std::string why;
                if (!ParseMonthKey(key, false, y, m) || !v.is_number() ||
                    !Validation::ValidateAmount(v.get<double>(), limit, why)) {
                    rep.skippedBudgets++;
                    rep.messages.push_back("Monthly budget '" + key + "' skipped (invalid month or amount).");
                    continue;
                }
                data.budgets.SetOverallBudget(y, m, limit);
            }
        }
        auto cb = j.find("categoryBudgets");
        if (cb != j.end() && cb->is_object()) {
            for (auto& [key, month] : cb->items()) {
                int y, m;
                if (!ParseMonthKey(key, true, y, m) || !month.is_object()) {
                    rep.skippedBudgets++;
                    rep.messages.push_back("Category budgets for '" + key + "' skipped (invalid month).");
                    continue;
                }
                for (auto& [cat, v] : month.items()) {
                    Money limit;
                    std::string why;
                    if (!v.is_number() || !Validation::ValidateAmount(v.get<double>(), limit, why)) {
                        rep.skippedBudgets++;
                        rep.messages.push_back("Budget for '" + cat + "' in " + key + " skipped (invalid amount).");
                        continue;
                    }
                    data.budgets.SetCategoryBudget(y, m, Validation::NormalizeCategory(cat), limit);
                }
            }
        }

        rep.loadedExpenses = static_cast<int>(data.expenses.size());
        rep.loadedRules = static_cast<int>(data.rules.size());
        rep.loadedGoals = static_cast<int>(data.goals.size());
        out = std::move(data);
        return true;
    } catch (const json::exception& e) {
        error = std::string("The file is not valid JSON: ") + e.what();
    } catch (const std::exception& e) {
        error = std::string("The file could not be read: ") + e.what();
    }
    return false;
}

bool Deserialize(const std::string& text, LedgerData& out, LoadReport& rep, std::string& error) {
    try {
        return ParseLedgerJson(json::parse(text), out, rep, error);
    } catch (const json::exception& e) {
        error = std::string("The file is not valid JSON: ") + e.what();
    } catch (const std::exception& e) {
        error = std::string("The file could not be read: ") + e.what();
    }
    return false;
}

// ── Full backup (ledger + settings + category rules in one file) ──

std::string SerializeFullBackup(const ExpenseTracker& tracker, const Settings& settings,
                                const AutoCategorizer& categorizer, const Date& today) {
    json j;
    j["expenseTrackerBackup"] = kFullBackupVersion;
    j["createdAt"] = today.ToString();
    j["ledger"] = BuildLedgerJson(tracker);
    j["settings"] = {{"theme", settings.theme}, {"syncRatesOnLaunch", settings.syncRatesOnLaunch}};
    j["categoryRules"] = categorizer.GetRules();
    return j.dump(2, ' ', false, json::error_handler_t::replace);
}

IoResult SaveFullBackup(const ExpenseTracker& tracker, const Settings& settings, const AutoCategorizer& categorizer,
                        const Date& today, const std::filesystem::path& filepath) {
    try {
        AtomicWriteOptions opts;
        opts.keepBackup = false;
        return WriteFileAtomic(filepath, SerializeFullBackup(tracker, settings, categorizer, today), opts);
    } catch (const std::exception& e) {
        return IoResult::Fail(std::string("Writing the backup failed: ") + e.what());
    }
}

IoResult LoadBackupFile(const std::filesystem::path& filepath, LedgerData& out, LoadReport& report,
                        BackupExtras& extras) {
    std::string text;
    IoResult r = ReadWholeFile(filepath, text);
    if (!r.ok) return r;
    if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    if (Utils::Trim(text).empty()) return IoResult::Fail("The file is empty.");

    extras = BackupExtras();
    std::string error;
    try {
        json j = json::parse(text);
        text.clear();
        text.shrink_to_fit();
        if (j.is_object() && j.contains("expenseTrackerBackup")) {
            extras.isFullBackup = true;
            auto created = j.find("createdAt");
            if (created != j.end() && created->is_string()) extras.createdAt = created->get<std::string>();
            auto ledger = j.find("ledger");
            if (ledger == j.end() || !ledger->is_object()) return IoResult::Fail("The backup has no ledger data.");

            auto st = j.find("settings");
            if (st != j.end() && st->is_object()) {
                extras.hasSettings = true;
                std::string theme;
                if (GetString(*st, "theme", theme)) extras.theme = theme;
                GetBool(*st, "syncRatesOnLaunch", extras.syncRatesOnLaunch);
            }
            auto rules = j.find("categoryRules");
            if (rules != j.end() && rules->is_object()) {
                extras.hasRules = true;
                for (auto& [keyword, category] : rules->items()) {
                    if (category.is_string()) extras.rules[keyword] = category.get<std::string>();
                }
            }
            if (!ParseLedgerJson(*ledger, out, report, error)) return IoResult::Fail(error);
            return IoResult::Ok();
        }
        if (!ParseLedgerJson(j, out, report, error)) return IoResult::Fail(error);
        return IoResult::Ok();
    } catch (const json::exception& e) {
        return IoResult::Fail(std::string("The file is not valid JSON: ") + e.what());
    } catch (const std::exception& e) {
        return IoResult::Fail(std::string("The file could not be read: ") + e.what());
    }
}

IoResult Load(const std::filesystem::path& filepath, LedgerData& out, LoadReport& report) {
    std::string text;
    IoResult r = ReadWholeFile(filepath, text);
    if (!r.ok) return r;
    if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    if (Utils::Trim(text).empty()) return IoResult::Fail("The data file is empty.");

    std::string error;
    if (!Deserialize(text, out, report, error)) return IoResult::Fail(error);
    return IoResult::Ok();
}

} // namespace JsonIO
