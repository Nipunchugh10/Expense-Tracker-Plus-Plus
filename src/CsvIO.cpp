#include "CsvIO.h"
#include "AutoCategorizer.h"
#include "ExpenseTracker.h"
#include "Utils.h"
#include "Validation.h"
#include <map>

namespace CsvIO {

namespace {

struct Columns {
    int id = -1, date = -1, type = -1, description = -1, amount = -1, currency = -1, category = -1, month = -1;
};

bool DetectHeader(const std::vector<std::string>& row, Columns& cols) {
    std::map<std::string, int*> names = {
        {"id", &cols.id}, {"date", &cols.date}, {"type", &cols.type},
        {"description", &cols.description}, {"amount", &cols.amount},
        {"currency", &cols.currency}, {"category", &cols.category}, {"month", &cols.month},
    };
    bool any = false;
    for (size_t i = 0; i < row.size(); i++) {
        auto it = names.find(Utils::ToLower(Utils::Trim(row[i])));
        if (it != names.end() && *it->second < 0) {
            *it->second = static_cast<int>(i);
            any = true;
        }
    }
    return any && cols.amount >= 0 && cols.description >= 0;
}

std::string Field(const std::vector<std::string>& row, int index) {
    if (index < 0 || static_cast<size_t>(index) >= row.size()) return "";
    return Utils::Trim(Utils::UnprotectCSVField(row[static_cast<size_t>(index)]));
}

} // namespace

std::string ExportToString(const ExpenseTracker& tracker) {
    std::string out = "ID,Date,Type,Description,Amount,Currency,Category\r\n";
    for (auto& e : tracker.GetExpenses()) {
        out += std::to_string(e.GetID()) + "," +
               e.GetDate().ToString() + "," +
               TransactionTypeToString(e.GetType()) + "," +
               Utils::EscapeCSVField(e.GetDescription(), true) + "," +
               MoneyUtil::ToDecimalString(e.GetAmount()) + "," +   // fixed 2 decimals, never scientific
               Utils::EscapeCSVField(e.GetCurrency(), true) + "," +
               Utils::EscapeCSVField(e.GetCategory(), true) + "\r\n";
    }
    return out;
}

IoResult Export(const ExpenseTracker& tracker, const std::filesystem::path& filepath) {
    // UTF-8 BOM so Excel opens non-ASCII text correctly.
    AtomicWriteOptions opts;
    opts.keepBackup = false;
    return WriteFileAtomic(filepath, "\xEF\xBB\xBF" + ExportToString(tracker), opts);
}

std::string DecodeText(const std::string& raw, bool& convertedFromCp1252) {
    std::string text = raw;
    if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    convertedFromCp1252 = false;
    if (!Utils::IsValidUtf8(text)) {
        text = Utils::Cp1252ToUtf8(text);
        convertedFromCp1252 = true;
    }
    return text;
}

bool IsLegacyFormat(const std::string& content) {
    auto rows = Utils::ParseCSV(content);
    if (rows.empty()) return false;
    Columns cols;
    return DetectHeader(rows[0], cols) && cols.month >= 0 && cols.date < 0;
}

ParseResult Parse(const std::string& content, int legacyYear, const AutoCategorizer* categorizer) {
    ParseResult result;
    auto rows = Utils::ParseCSV(content);
    if (rows.empty()) return result;

    Columns cols;
    size_t firstData = 0;
    if (DetectHeader(rows[0], cols)) {
        firstData = 1;
    } else {
        // No header: assume the current export order without a Type column.
        cols.id = 0; cols.date = 1; cols.description = 2; cols.amount = 3; cols.currency = 4; cols.category = 5;
    }
    const bool legacy = cols.month >= 0 && cols.date < 0;

    for (size_t r = firstData; r < rows.size(); r++) {
        const auto& row = rows[r];
        const std::string line = "Row " + std::to_string(r + 1) + ": ";
        auto skip = [&](const std::string& why) {
            result.skipped++;
            result.reasons.push_back(line + why);
        };

        Money amount;
        std::string amountText = Field(row, cols.amount);
        if (!MoneyUtil::ParseDecimal(amountText, amount)) { skip("amount '" + amountText + "' is not a number"); continue; }
        if (amount < 0) { skip("negative amount (set Type to income instead of using a minus sign)"); continue; }

        Date date;
        if (legacy) {
            std::string monthText = Field(row, cols.month);
            int month = 0;
            for (char c : monthText) {
                if (c < '0' || c > '9' || month > 12) { month = 0; break; }
                month = month * 10 + (c - '0');
            }
            if (month < 1 || month > 12) { skip("month '" + monthText + "' is not 1-12"); continue; }
            date = {legacyYear, month, 1};
        } else {
            std::string dateText = Field(row, cols.date);
            if (!Date::TryParse(dateText, date)) { skip("date '" + dateText + "' is not a valid YYYY-MM-DD date"); continue; }
        }

        TransactionType type = TransactionType::Expense;
        std::string typeText = Field(row, cols.type);
        if (!typeText.empty() && !TransactionTypeFromString(typeText, type)) {
            skip("unknown type '" + typeText + "' (use expense, income, transfer or subscription)");
            continue;
        }

        std::string currency = Field(row, cols.currency);
        if (currency.empty()) currency = kDefaultCurrency;

        std::string category = Field(row, cols.category);
        bool suggested = false;
        if (category.empty() && categorizer) {
            category = categorizer->SuggestCategory(Field(row, cols.description));
            suggested = !category.empty();
        }

        Expense e(0, Field(row, cols.description), amount, date, category, currency, 0, type);
        std::string why;
        if (!Validation::NormalizeExpense(e, why)) { skip(why); continue; }
        if (suggested) result.autoCategorized++;
        result.drafts.push_back(e);
    }
    return result;
}

} // namespace CsvIO
