#pragma once
#include <filesystem>
#include <string>

// Single source of truth for every file location (P0-A7).
//
// Data lives in %APPDATA%\ExpenseTrackerPlusPlus so it does not depend on the
// working directory and survives deleting the build folder. The Microsoft Store
// (MSIX) build uses Documents\Expense Tracker Plus Plus instead and copies the
// %APPDATA% data there on first run. The environment variable
// EXPENSE_TRACKER_DATA_DIR overrides the location (portable use, tests).
namespace Paths {
    void Init();
    bool IsPackaged();   // running as the Microsoft Store (MSIX) package
    void SetDataDirForTesting(const std::filesystem::path& dir);

    std::filesystem::path DataDir();
    std::filesystem::path LedgerFile();          // expenses.json
    std::filesystem::path SettingsFile();        // settings.json
    std::filesystem::path CategoryRulesFile();   // category_rules.json

    // Copies a ledger from older locations (<cwd>/data, <exe dir>/data) when
    // the data directory has none yet. Returns a message, or "" if nothing moved.
    std::string MigrateLegacyLocations();

    // "<stem>.<tag>-YYYYMMDD-HHMMSS<ext>" next to `file`.
    std::filesystem::path TimestampedSibling(const std::filesystem::path& file, const std::string& tag);

    std::string ToUtf8(const std::filesystem::path& p);
    std::filesystem::path FromUtf8(const std::string& s);
    std::filesystem::path ExecutableDir();
}
