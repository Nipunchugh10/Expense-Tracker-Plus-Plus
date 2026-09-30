#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include "AtomicFile.h"

class ExpenseTracker;
class AutoCategorizer;
struct LedgerData;
struct Settings;
struct Date;

namespace JsonIO {
    constexpr int kSchemaVersion = 2;

    struct LoadReport {
        int fileVersion = 1;
        bool newerVersion = false;       // written by a newer app: open read-only (P0-A11)
        int loadedExpenses = 0;
        int skippedExpenses = 0;
        int loadedRules = 0;
        int skippedRules = 0;
        int loadedGoals = 0;
        int skippedGoals = 0;
        int skippedBudgets = 0;
        int reassignedIds = 0;
        std::vector<std::string> messages;   // per-record details for the user

        int TotalSkipped() const { return skippedExpenses + skippedRules + skippedGoals + skippedBudgets; }
    };

    std::string Serialize(const ExpenseTracker& tracker);
    IoResult Save(const ExpenseTracker& tracker, const std::filesystem::path& filepath);

    // Never throws. Invalid records are skipped and reported; the tracker is
    // not touched (the caller applies `out` with ExpenseTracker::ReplaceAll).
    bool Deserialize(const std::string& text, LedgerData& out, LoadReport& report, std::string& error);
    IoResult Load(const std::filesystem::path& filepath, LedgerData& out, LoadReport& report);

    // ── Full backup: one file with the ledger, settings and category rules ──
    // Used to move everything to a new computer (File > Export Full Backup...,
    // welcome screen > "I have a backup").
    constexpr int kFullBackupVersion = 1;

    struct BackupExtras {
        bool isFullBackup = false;       // false = a plain expenses.json / .bak / JSON export
        std::string createdAt;
        bool hasSettings = false;
        std::string theme;
        bool syncRatesOnLaunch = true;
        bool hasRules = false;
        std::map<std::string, std::string> rules;
    };

    std::string SerializeFullBackup(const ExpenseTracker& tracker, const Settings& settings,
                                    const AutoCategorizer& categorizer, const Date& today);
    IoResult SaveFullBackup(const ExpenseTracker& tracker, const Settings& settings, const AutoCategorizer& categorizer,
                            const Date& today, const std::filesystem::path& filepath);
    // Accepts a full backup or any plain ledger file. Never throws.
    IoResult LoadBackupFile(const std::filesystem::path& filepath, LedgerData& out, LoadReport& report,
                            BackupExtras& extras);
}
