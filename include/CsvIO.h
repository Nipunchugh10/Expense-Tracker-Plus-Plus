#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "AtomicFile.h"
#include "Expense.h"

class ExpenseTracker;

class AutoCategorizer;

namespace CsvIO {
    struct ParseResult {
        std::vector<Expense> drafts;        // validated rows; IDs are ignored on import
        int skipped = 0;
        std::vector<std::string> reasons;   // one line per skipped row
        bool convertedFromCp1252 = false;
        int autoCategorized = 0;            // rows without a category that got one from the rules
        std::vector<std::string> notes;     // how the file was read (separator, decimal mark, date order)
    };

    // How to read a file when the file itself does not tell (the app fills these from the
    // Windows region settings). Everything else (separator, decimal mark, date order) is
    // detected from the file.
    struct ParseOptions {
        std::string defaultCurrency = kDefaultCurrency;   // for rows with an empty Currency column
        bool decimalCommaWhenUnsure = false;              // "1.500": 1500 (true) or 1.5 (false)
        bool dayFirstWhenUnsure = true;                   // "03/04/2026": 3 April (true) or 4 March
    };

    // Columns: ID,Date,Type,Description,Amount,Currency,Category
    std::string ExportToString(const ExpenseTracker& tracker);
    IoResult Export(const ExpenseTracker& tracker, const std::filesystem::path& filepath);

    // Strips a UTF-8 BOM and converts Windows-1252 text to UTF-8 when needed.
    std::string DecodeText(const std::string& raw, bool& convertedFromCp1252);

    // Legacy exports have a "Month" column and no dates; they need a year.
    bool IsLegacyFormat(const std::string& decodedContent);

    // Rows with an empty Category get a suggestion from `categorizer` when one is given
    // (otherwise, or when no rule matches, they fall back to "General").
    // The separator (',', ';', tab), the decimal mark and the date order (YYYY-MM-DD,
    // DD/MM/YYYY, MM/DD/YYYY, DD.MM.YYYY) are detected per file.
    ParseResult Parse(const std::string& decodedContent, int legacyYear, const AutoCategorizer* categorizer = nullptr,
                      const ParseOptions& options = ParseOptions());
}
