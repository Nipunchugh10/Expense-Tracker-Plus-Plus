#pragma once
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <thread>
#include <string>
#include <vector>
#include "AppContext.h"
#include "AutoCategorizer.h"
#include "CsvIO.h"
#include "JsonIO.h"
#include "CommandManager.h"
#include "ExpenseTracker.h"
#include "RateSync.h"
#include "ReportGenerator.h"
#include "Settings.h"
#include "ThemeManager.h"
#include "Tabs/AnalyticsTab.h"
#include "Tabs/BudgetTab.h"
#include "Tabs/DashboardTab.h"
#include "Tabs/ExpensesTab.h"
#include "Tabs/GoalsTab.h"
#include "Tabs/SubscriptionsTab.h"
#include "WelcomeScreen.h"

struct GLFWwindow;

class App {
public:
    App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    void Init(GLFWwindow* window);   // after the ImGui/ImPlot contexts exist
    void PreFrame();                 // before ImGui::NewFrame (style changes)
    void Render();                   // one frame of UI

    // Called when the window wants to close. True when it is safe to exit
    // (nothing to save, or saved). False with a reason when saving failed.
    bool SaveBeforeExit(std::string& error);

private:
    enum class ImportStage { None, AskYear, AskDuplicates };

    void SetStatus(StatusLevel level, const std::string& message, std::vector<std::string> details = {});
    void Queue(std::function<void()> fn) { pending.push_back(std::move(fn)); }
    void ApplyPending();
    bool MutationsAllowed();   // false (with a warning) in read-only / blocked mode

    void LoadLedger();
    bool SaveLedger(bool quiet);
    void SaveCategorizer();
    void SaveSettings();
    void RequestExit();

    void RequestRecurringGeneration(bool userInitiated);
    void DoGenerate(bool skipBacklog, bool announce);
    void DoReset();
    void StartRateSync(bool userInitiated);
    void PollRateSync();
    void DoUndo();
    void DoRedo();
    void CheckDayRollover();

    void StartImport();
    void ContinueImport(int legacyYear);
    void ApplyParsedImport(CsvIO::ParseResult&& parsed);
    void StartRestore();
    void DoRestore(bool replace);
    void PollJobs();
    void CommitImport(bool skipDuplicates);
    void ExportCsv();
    void ExportJson();
    void ExportReport(ReportGenerator::Format format);
    void ExportFullBackup();
    void RenderWelcome();
    void FinishWelcome();

    void RenderMenuBar();
    void RenderStatusBar();
    void RenderModals();
    void RenderLoadFailedModal();
    void RenderImportModals();
    void RenderBackfillModal();
    void RenderRatesModal();
    void RenderRulesModal();
    void RenderReportModal();
    void RenderDetailsModal();
    void RenderRestoreModal();
    void RenderBusyModal();

    // Runs `work` on a worker thread (file reading/parsing) and calls `onDone`
    // on the UI thread when it finishes. A "Working..." dialog blocks input
    // meanwhile, so the ledger cannot change underneath the job. The worker
    // owns its state, so closing the app never waits for it.
    template <class R>
    void RunJob(const std::string& label, std::function<R()> work, std::function<void(R&)> onDone) {
        struct State {
            std::atomic<bool> done{false};
            R result;
        };
        auto state = std::make_shared<State>();
        try {
            std::thread([state, work]() {
                try {
                    state->result = work();
                } catch (...) {
                    // result stays default-constructed, which reports failure
                }
                state->done.store(true, std::memory_order_release);
            }).detach();
        } catch (const std::exception& e) {
            SetStatus(StatusLevel::Error, std::string("Could not start background work: ") + e.what());
            return;
        }
        jobs.push_back({label, std::chrono::steady_clock::now(), [state, onDone]() {
            if (!state->done.load(std::memory_order_acquire)) return false;
            onDone(state->result);
            return true;
        }});
    }
    void HandleShortcuts();

    ExpenseTracker   tracker;
    CommandManager   commands;
    AutoCategorizer  categorizer;
    Settings         settings;
    AppContext       ctx;
    GLFWwindow*      window = nullptr;

    DashboardTab     dashboardTab;
    ExpensesTab      expensesTab;
    SubscriptionsTab subscriptionsTab;
    BudgetTab        budgetTab;
    GoalsTab         goalsTab;
    AnalyticsTab     analyticsTab;

    std::vector<std::function<void()>> pending;   // applied after the frame's widgets (P0-E2)

    // Status bar
    std::string              statusMessage;
    StatusLevel              statusLevel = StatusLevel::Info;
    float                    statusTimer = 0.0f;
    std::vector<std::string> statusDetails;
    bool                     openDetails = false;

    // Persistence state
    float autoSaveTimer = 0.0f;
    float autoSaveInterval = 60.0f;
    bool  saveBlocked = false;         // corrupt file not yet resolved, or newer schema
    bool  readOnly = false;
    std::string saveBlockedReason;
    bool  loadFailed = false;
    std::string loadFailedMessage;
    bool  loadBackupOk = false;
    std::filesystem::path loadBackupPath;
    int   loadedVersion = 0;
    bool  versionBackupDone = false;

    // Day rollover
    Date  lastDay;
    float dayCheckTimer = 0.0f;
    float minuteCheckTimer = 0.0f;

    // Import wizard
    ImportStage importStage = ImportStage::None;
    bool        openImportPopup = false;
    std::string importContent;
    std::string importFileName;
    bool        importCp1252 = false;
    int         importYear = 0;
    std::vector<Expense> importDrafts;
    std::vector<std::string> importReasons;
    int         importSkipped = 0;
    size_t      importDuplicates = 0;

    // Background jobs (see RunJob)
    struct Job {
        std::string label;
        std::chrono::steady_clock::time_point started;
        std::function<bool()> poll;   // true once finished (and handled)
    };
    std::vector<Job> jobs;

    // Restore from backup
    struct RestoreLoad {
        IoResult io;
        LedgerData data;
        JsonIO::LoadReport report;
        std::uintmax_t bytes = 0;
        double seconds = 0.0;
        std::filesystem::path file;
        JsonIO::BackupExtras extras;
    };
    std::unique_ptr<RestoreLoad> pendingRestore;
    bool restoreFromWelcome = false;

    // Welcome screen (first run, or View > Welcome Screen)
    WelcomeScreen welcome;
    bool showWelcome = false;
    bool welcomeHasData = false;
    bool openFreshConfirm = false;
    bool openRestorePopup = false;

    // Recurring backfill confirmation
    bool openBackfill = false;
    bool backfillSuppressed = false;
    PendingOccurrences pendingBackfill;

    // Tool dialogs
    bool openRates = false;
    bool openRules = false;
    bool openReport = false;
    std::string newRateCode;
    double      newRateValue = 0.0;
    std::string ratesError;
    std::string newRuleKeyword;
    std::string newRuleCategory;
    std::string rulesFilter;
    std::string rulesError;
    int reportYear = 0;
    int reportMonth = 0;

    // Exchange-rate sync: a detached worker fills this shared state, so closing
    // the app never waits for the network.
    struct RateSyncState {
        std::atomic<bool> done{false};
        RateSync::Result  result;
    };
    std::shared_ptr<RateSyncState> rateSync;
    bool rateSyncUserInitiated = false;

    bool themeChangeRequested = false;
    AppTheme requestedTheme = AppTheme::DarkModern;
};
