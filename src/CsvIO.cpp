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

// A date split into three numbers: "2026-01-05", "05.01.2026", "1/5/2026 10:30" (time ignored).
struct DateParts {
    int value[3] = {0, 0, 0};
    size_t width[3] = {0, 0, 0};
    char separator = 0;
};

bool SplitDate(const std::string& text, DateParts& p) {
    std::string s = text.substr(0, text.find_first_of(" T"));
    p = DateParts();
    int part = 0;
    for (char c : s) {
        if (c >= '0' && c <= '9') {
            if (p.width[part] >= 4) return false;
            p.value[part] = p.value[part] * 10 + (c - '0');
            p.width[part]++;
        } else if ((c == '-' || c == '/' || c == '.') && part < 2 && p.width[part] > 0) {
            if (part == 0) p.separator = c;
            else if (c != p.separator) return false;
            part++;
        } else {
            return false;
        }
    }
    return part == 2 && p.width[2] > 0;
}

bool YearFirst(const DateParts& p) { return p.width[0] == 4 && p.width[1] <= 2 && p.width[2] <= 2; }
bool YearLast(const DateParts& p)  { return p.width[2] == 4 && p.width[0] <= 2 && p.width[1] <= 2; }

bool ParseDate(const std::string& text, bool dayFirst, Date& out) {
    DateParts p;
    if (!SplitDate(text, p)) return false;
    Date d;
    if (YearFirst(p))     d = Date{p.value[0], p.value[1], p.value[2]};
    else if (YearLast(p)) d = dayFirst ? Date{p.value[2], p.value[1], p.value[0]} : Date{p.value[2], p.value[0], p.value[1]};
    else return false;
    if (!d.IsValid()) return false;
    out = d;
    return true;
}

const char* DelimiterName(char c) {
    switch (c) {
        case ';':  return "semicolons (;)";
        case '\t': return "tabs";
        case '|':  return "bars (|)";
        default:   return "commas";
    }
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
    auto rows = Utils::ParseCSV(content, Utils::DetectCSVDelimiter(content));
    if (rows.empty()) return false;
    Columns cols;
    return DetectHeader(rows[0], cols) && cols.month >= 0 && cols.date < 0;
}

ParseResult Parse(const std::string& content, int legacyYear, const AutoCategorizer* categorizer,
                  const ParseOptions& options) {
    ParseResult result;
    const char delimiter = Utils::DetectCSVDelimiter(content);
    auto rows = Utils::ParseCSV(content, delimiter);
    if (rows.empty()) return result;
    if (delimiter != ',') result.notes.push_back(std::string("Fields are separated by ") + DelimiterName(delimiter) + ".");

    Columns cols;
    size_t firstData = 0;
    if (DetectHeader(rows[0], cols)) {
        firstData = 1;
    } else {
        // No header: assume the current export order without a Type column.
        cols.id = 0; cols.date = 1; cols.description = 2; cols.amount = 3; cols.currency = 4; cols.category = 5;
    }
    const bool legacy = cols.month >= 0 && cols.date < 0;

    // Decide the decimal mark and the date order from the whole file before reading any row,
    // so "12,50" is never read as 1250 just because it appears before "3.40".
    std::vector<std::string> amountSamples;
    int dayFirstVotes = 0, monthFirstVotes = 0;
    bool otherDateOrder = false;
    for (size_t r = firstData; r < rows.size(); r++) {
        amountSamples.push_back(Field(rows[r], cols.amount));
        if (legacy) continue;
        DateParts p;
        if (!SplitDate(Field(rows[r], cols.date), p) || !YearLast(p)) continue;
        otherDateOrder = true;
        if (p.separator == '.' || p.value[0] > 12) dayFirstVotes++;   // 05.01.2026 is always day first
        if (p.value[1] > 12) monthFirstVotes++;
    }
    const MoneyUtil::DecimalMark fallbackMark =
        options.decimalCommaWhenUnsure ? MoneyUtil::DecimalMark::Comma : MoneyUtil::DecimalMark::Dot;
    const MoneyUtil::DecimalMark mark = MoneyUtil::DetectDecimalMark(amountSamples, fallbackMark);
    const bool markFromFile = MoneyUtil::DetectDecimalMark(amountSamples, MoneyUtil::DecimalMark::Dot) ==
                              MoneyUtil::DetectDecimalMark(amountSamples, MoneyUtil::DecimalMark::Comma);
    if (mark == MoneyUtil::DecimalMark::Comma) {
        result.notes.push_back(std::string("Amounts were read with a comma as the decimal mark (1.234,56)") +
                               (markFromFile ? "." : ", as in your Windows region settings."));
    }
    const bool dayFirst = dayFirstVotes > monthFirstVotes   ? true
                        : monthFirstVotes > dayFirstVotes   ? false
                                                            : options.dayFirstWhenUnsure;
    if (otherDateOrder) {
        result.notes.push_back(std::string("Dates were read as ") + (dayFirst ? "day/month/year" : "month/day/year") +
                               (dayFirstVotes == monthFirstVotes ? ", as in your Windows region settings." : "."));
    }
    const char decimalChar = mark == MoneyUtil::DecimalMark::Comma ? ',' : '.';

    for (size_t r = firstData; r < rows.size(); r++) {
        const auto& row = rows[r];
        const std::string line = "Row " + std::to_string(r + 1) + ": ";
        auto skip = [&](const std::string& why) {
            result.skipped++;
            result.reasons.push_back(line + why);
        };

        Money amount;
        std::string amountText = Field(row, cols.amount);
        if (!MoneyUtil::ParseAmount(amountText, mark, amount)) {
            const char otherMark = decimalChar == '.' ? ',' : '.';
            skip("amount '" + amountText + "' is not a number" +
                 (amountText.find(otherMark) != std::string::npos
                      ? std::string(" in this file's format (decimals are written with '") + decimalChar + "')"
                      : std::string()));
            continue;
        }
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
            if (!ParseDate(dateText, dayFirst, date)) {
                skip("date '" + dateText + "' is not a valid date (use YYYY-MM-DD, DD/MM/YYYY or MM/DD/YYYY)");
                continue;
            }
        }

        TransactionType type = TransactionType::Expense;
        std::string typeText = Field(row, cols.type);
        if (!typeText.empty() && !TransactionTypeFromString(typeText, type)) {
            skip("unknown type '" + typeText + "' (use expense, income, transfer, subscription or savings)");
            continue;
        }

        std::string currency = Field(row, cols.currency);
        if (currency.empty()) currency = options.defaultCurrency;

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
