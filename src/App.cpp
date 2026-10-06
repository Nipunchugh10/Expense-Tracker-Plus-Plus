#include "App.h"
#include "AppInfo.h"
#include "CsvIO.h"
#include "JsonIO.h"
#include "Paths.h"
#include "UiHelpers.h"
#include "Utils.h"
#include "Validation.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdio>
#include <thread>

namespace fs = std::filesystem;

namespace {
constexpr int kBackfillConfirmThreshold = 366;   // occurrences per rule before asking (P0-D4)
constexpr int kHardBackfillCap = 50000;
}

App::App() {
    ctx.tracker = &tracker;
    ctx.categorizer = &categorizer;
    ctx.defer = [this](std::function<bool(ExpenseTracker&)> fn) {
        Queue([this, fn]() {
            if (!MutationsAllowed()) return;
            if (!fn(tracker)) SetStatus(StatusLevel::Error, tracker.GetLastError());
        });
    };
    ctx.deferCommand = [this](std::function<std::unique_ptr<ICommand>(ExpenseTracker&)> factory) {
        Queue([this, factory]() {
            if (!MutationsAllowed()) return;
            std::string err;
            if (!commands.ExecuteCommand(factory(tracker), err)) {
                SetStatus(StatusLevel::Error, err.empty() ? "The change could not be applied." : err);
            }
        });
    };
    ctx.status = [this](StatusLevel level, const std::string& msg) { SetStatus(level, msg); };
    ctx.requestRecurringGeneration = [this]() { Queue([this]() { RequestRecurringGeneration(true); }); };
    ctx.requestReset = [this]() { Queue([this]() { DoReset(); }); };
    ctx.saveCategorizer = [this]() { SaveCategorizer(); };
}

// ── Lifecycle ─────────────────────────────────────────────────────

void App::Init(GLFWwindow* w) {
    window = w;
    Paths::Init();
    std::string migration = Paths::MigrateLegacyLocations();

    IoResult s = settings.Load(Paths::SettingsFile());
    ThemeManager::ApplyTheme(ThemeManager::ThemeFromName(settings.theme));

    IoResult c = categorizer.Load(Paths::CategoryRulesFile());

    std::error_code ec;
    const bool ledgerExisted = fs::exists(Paths::LedgerFile(), ec);
    LoadLedger();
    // First run on this computer: greet the user instead of an empty dashboard.
    if (!ledgerExisted && !loadFailed && !settings.welcomeCompleted) {
        showWelcome = true;
        welcomeHasData = false;
    }
    if (!migration.empty()) SetStatus(StatusLevel::Info, migration);
    if (!s.ok) SetStatus(StatusLevel::Warning, s.message);
    if (!c.ok) SetStatus(StatusLevel::Warning, c.message);

    lastDay = Date::Today();
    if (!saveBlocked) RequestRecurringGeneration(false);
    if (settings.syncRatesOnLaunch && !saveBlocked) StartRateSync(false);
}

void App::StartRateSync(bool userInitiated) {
    if (rateSync) return;   // already running
    if (saveBlocked) {
        if (userInitiated) SetStatus(StatusLevel::Warning, saveBlockedReason);
        return;
    }
    rateSyncUserInitiated = userInitiated;
    auto state = std::make_shared<RateSyncState>();
    rateSync = state;
    try {
        std::thread([state]() {
            state->result = RateSync::FetchLatest();
            state->done.store(true, std::memory_order_release);
        }).detach();
    } catch (const std::exception& e) {
        rateSync.reset();
        SetStatus(StatusLevel::Warning, std::string("Could not start the exchange-rate sync: ") + e.what());
    }
}

void App::PollRateSync() {
    if (!rateSync || !rateSync->done.load(std::memory_order_acquire)) return;
    RateSync::Result r = std::move(rateSync->result);
    bool user = rateSyncUserInitiated;
    rateSync.reset();

    // A background result never hides an important message from startup.
    bool mayReplace = user || statusTimer <= 0.0f ||
                      statusLevel == StatusLevel::Info || statusLevel == StatusLevel::Success;
    const CurrencyManager& cm = tracker.GetCurrency();

    if (!r.ok) {
        std::string saved = cm.GetAsOf().empty() ? "built-in approximate rates" : "rates from " + cm.GetAsOf();
        if (mayReplace) {
            SetStatus(user ? StatusLevel::Warning : StatusLevel::Info,
                      "Exchange rates not updated (" + r.error + "). Using " + saved + ".");
        }
        return;
    }
    if (saveBlocked) return;
    int changed = tracker.ApplySyncedRates(r.pivotRates, r.asOf, r.source);
    if (mayReplace) {
        std::string when = r.asOf.empty() ? "" : " as of " + r.asOf;
        SetStatus(StatusLevel::Success, changed > 0
            ? "Exchange rates updated" + when + " (" + std::to_string(changed) + " currencies, " + r.source + ")."
            : "Exchange rates are up to date" + when + " (" + r.source + ").");
    }
}

void App::PreFrame() {
    if (themeChangeRequested) {
        ThemeManager::ApplyTheme(requestedTheme);
        themeChangeRequested = false;
    }
}

bool App::SaveBeforeExit(std::string& error) {
    if (!tracker.HasChanges() || saveBlocked) return true;
    if (showWelcome && !welcomeHasData) return true;   // user has not chosen how to start yet
    if (SaveLedger(false)) return true;
    error = statusMessage;
    return false;
}

void App::RequestExit() {
    if (window) glfwSetWindowShouldClose(window, GLFW_TRUE);
}

void App::SetStatus(StatusLevel level, const std::string& message, std::vector<std::string> details) {
    statusLevel = level;
    statusMessage = message;
    statusTimer = level == StatusLevel::Error ? 12.0f : level == StatusLevel::Warning ? 9.0f : 5.0f;
    statusDetails = std::move(details);
}

bool App::MutationsAllowed() {
    if (!readOnly && !saveBlocked) return true;
    SetStatus(StatusLevel::Warning, saveBlockedReason.empty() ? "Changes are disabled." : saveBlockedReason);
    return false;
}

void App::ApplyPending() {
    if (pending.empty()) return;
    auto work = std::move(pending);
    pending.clear();
    for (auto& fn : work) fn();
}

// ── Persistence ───────────────────────────────────────────────────

void App::LoadLedger() {
    loadFailed = false;
    saveBlocked = false;
    readOnly = false;
    saveBlockedReason.clear();
    commands.Clear();

    const fs::path path = Paths::LedgerFile();
    std::error_code ec;
    bool exists = fs::exists(path, ec);
    if (ec) {
        loadFailed = true;
        saveBlocked = true;
        saveBlockedReason = "Saving is disabled until the data file problem is resolved.";
        loadFailedMessage = "Cannot access " + Paths::ToUtf8(path) + ": " + ec.message();
        loadBackupOk = false;
        return;
    }
    if (!exists) {
        loadedVersion = JsonIO::kSchemaVersion;
        SetStatus(StatusLevel::Info, "No saved data found. Starting fresh. Data folder: " + Paths::ToUtf8(Paths::DataDir()));
        return;
    }

    LedgerData data;
    JsonIO::LoadReport report;
    const auto loadStart = std::chrono::steady_clock::now();
    IoResult r = JsonIO::Load(path, data, report);
    const double loadSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - loadStart).count();
    if (!r.ok) {
        // Never overwrite a ledger we could not read (P0-A4): back it up and block saving.
        loadFailed = true;
        saveBlocked = true;
        saveBlockedReason = "Saving is disabled until the data file problem is resolved.";
        loadFailedMessage = r.message;
        loadBackupPath = Paths::TimestampedSibling(path, "corrupt");
        IoResult b = CopyFileSafe(path, loadBackupPath);
        loadBackupOk = b.ok;
        if (!b.ok) loadFailedMessage += "\n\nThe backup copy also failed: " + b.message;
        return;
    }

    tracker.ReplaceAll(std::move(data));
    loadedVersion = report.fileVersion;
    versionBackupDone = false;

    if (report.newerVersion) {
        readOnly = true;
        saveBlocked = true;
        saveBlockedReason = "This data file was written by a newer version of the app (schema " +
                            std::to_string(report.fileVersion) + "). It is open read-only so nothing is lost.";
        SetStatus(StatusLevel::Warning, saveBlockedReason);
        return;
    }

    std::string msg = "Loaded " + std::to_string(report.loadedExpenses) + " transaction(s)";
    if (report.loadedRules) msg += ", " + std::to_string(report.loadedRules) + " subscription(s)";
    if (report.loadedGoals) msg += ", " + std::to_string(report.loadedGoals) + " goal(s)";
    if (loadSeconds >= 0.3) {
        char took[48];
        std::snprintf(took, sizeof(took), " in %.1f s", loadSeconds);
        msg += took;
    }
    msg += ".";
    if (report.TotalSkipped() > 0) {
        msg += " " + std::to_string(report.TotalSkipped()) + " invalid record(s) were skipped - see Details.";
        SetStatus(StatusLevel::Warning, msg, report.messages);
    } else {
        SetStatus(StatusLevel::Success, msg, report.messages);
    }
}

bool App::SaveLedger(bool quiet) {
    if (saveBlocked) {
        if (!quiet) SetStatus(StatusLevel::Warning, saveBlockedReason);
        return false;
    }
    const fs::path path = Paths::LedgerFile();
    std::error_code ec;

    // One-time copy of an older-schema file before it is upgraded (P0-A11).
    if (loadedVersion > 0 && loadedVersion < JsonIO::kSchemaVersion && !versionBackupDone) {
        if (fs::exists(path, ec)) {
            fs::path versioned = path.parent_path() / ("expenses.v" + std::to_string(loadedVersion) + ".bak.json");
            if (!fs::exists(versioned, ec)) {
                IoResult b = CopyFileSafe(path, versioned);
                if (!b.ok) {
                    SetStatus(StatusLevel::Error, "Could not back up the old data file before upgrading it: " + b.message);
                    return false;
                }
            }
        }
        versionBackupDone = true;
    }

    const auto saveStart = std::chrono::steady_clock::now();
    IoResult r = JsonIO::Save(tracker, path);
    const double saveSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - saveStart).count();
    if (!r.ok) {
        SetStatus(StatusLevel::Error, "Save failed: " + r.message);
        return false;
    }
    tracker.ClearDirty();
    loadedVersion = JsonIO::kSchemaVersion;
    autoSaveTimer = 0.0f;
    std::string msg = quiet ? "Auto-saved" : "Saved";
    if (saveSeconds >= 0.3) {
        char took[48];
        std::snprintf(took, sizeof(took), " in %.1f s", saveSeconds);
        msg += took;
    }
    SetStatus(quiet ? StatusLevel::Info : StatusLevel::Success, msg + ".");
    return true;
}

void App::SaveCategorizer() {
    IoResult r = categorizer.Save(Paths::CategoryRulesFile());
    if (!r.ok) SetStatus(StatusLevel::Error, "Could not save category rules: " + r.message);
}

void App::SaveSettings() {
    IoResult r = settings.Save(Paths::SettingsFile());
    if (!r.ok) SetStatus(StatusLevel::Warning, "Could not save settings: " + r.message);
}

// ── Actions ───────────────────────────────────────────────────────

void App::RequestRecurringGeneration(bool userInitiated) {
    if (saveBlocked) return;
    Date today = Date::Today();
    PendingOccurrences p = tracker.CountPendingOccurrences(today, Date::NowMinutes());
    if (p.total == 0) {
        if (userInitiated) SetStatus(StatusLevel::Info, "No pending bills to generate.");
        return;
    }
    if (p.maxPerRule > kBackfillConfirmThreshold) {
        if (!userInitiated && backfillSuppressed) return;
        pendingBackfill = p;
        openBackfill = true;
        return;
    }
    DoGenerate(false, userInitiated);
}

void App::DoGenerate(bool skipBacklog, bool announce) {
    Date today = Date::Today();
    if (skipBacklog) tracker.SkipRecurringBacklog(today);
    GenerationResult r = tracker.GenerateRecurringExpenses(today, kHardBackfillCap, Date::NowMinutes());
    // Background results never hide an unread warning or error (e.g. skipped records at load).
    const bool mayReplace = announce || statusTimer <= 0.0f ||
                            statusLevel == StatusLevel::Info || statusLevel == StatusLevel::Success;
    if ((r.generated > 0 && mayReplace) || announce) {
        std::string msg = "Recorded " + std::to_string(r.generated) + " subscription payment(s) automatically.";
        if (r.rulesCapped > 0) msg += " Some history was left for the next run (limit reached).";
        SetStatus(r.generated > 0 ? StatusLevel::Success : StatusLevel::Info, msg);
    }
}

void App::DoReset() {
    if (!MutationsAllowed()) return;
    // Back up the current state first (P0-A10).
    fs::path backup = Paths::TimestampedSibling(Paths::LedgerFile(), "before-reset");
    AtomicWriteOptions opts;
    opts.keepBackup = false;
    IoResult b = WriteFileAtomic(backup, JsonIO::Serialize(tracker), opts);
    if (!b.ok) {
        SetStatus(StatusLevel::Error, "Reset cancelled: the backup could not be written. " + b.message);
        return;
    }
    tracker.ResetAll();
    commands.Clear();
    if (SaveLedger(true)) {
        SetStatus(StatusLevel::Success, "All data was reset. A backup was saved to " + Paths::ToUtf8(backup));
    }
}

void App::DoUndo() {
    if (!MutationsAllowed()) return;
    std::string name, err;
    if (!commands.CanUndo()) {
        SetStatus(StatusLevel::Info, "Nothing to undo.");
        return;
    }
    if (commands.Undo(name, err)) SetStatus(StatusLevel::Success, "Undid " + name);
    else SetStatus(StatusLevel::Error, "Could not undo " + name + ": " + err);
}

void App::DoRedo() {
    if (!MutationsAllowed()) return;
    std::string name, err;
    if (!commands.CanRedo()) {
        SetStatus(StatusLevel::Info, "Nothing to redo.");
        return;
    }
    if (commands.Redo(name, err)) SetStatus(StatusLevel::Success, "Redid " + name);
    else SetStatus(StatusLevel::Error, "Could not redo " + name + ": " + err);
}

void App::CheckDayRollover() {
    dayCheckTimer += ImGui::GetIO().DeltaTime;
    if (dayCheckTimer < 5.0f) return;
    dayCheckTimer = 0.0f;
    Date today = Date::Today();
    if (today != lastDay) {
        lastDay = today;
        backfillSuppressed = false;
        Queue([this]() { RequestRecurringGeneration(false); });
    }
    // Subscriptions renew at a time of day, so re-check regularly, not only at midnight.
    minuteCheckTimer += 5.0f;
    if (minuteCheckTimer >= 30.0f) {
        minuteCheckTimer = 0.0f;
        Queue([this]() { RequestRecurringGeneration(false); });
    }
}

// ── Import / export ───────────────────────────────────────────────

void App::StartImport() {
    if (saveBlocked) {
        SetStatus(StatusLevel::Warning, saveBlockedReason);
        return;
    }
    fs::path file;
    if (!Ui::OpenFileDialog(L"CSV files (*.csv)\0*.csv\0All files (*.*)\0*.*\0", file)) return;
    importFileName = Paths::ToUtf8(file.filename());

    // Reading and parsing run on a worker thread so large files never freeze the window.
    struct CsvLoad {
        IoResult io;
        std::string content;   // kept only for legacy files (they still need a year)
        bool cp1252 = false;
        bool legacy = false;
        CsvIO::ParseResult parsed;
    };
    const int year = Date::Today().year;
    AutoCategorizer rules = categorizer;   // a copy: the worker must not touch the UI thread's object
    RunJob<CsvLoad>("Reading " + importFileName + "...",
        [file, year, rules]() {
            CsvLoad out;
            std::string raw;
            out.io = ReadWholeFile(file, raw);
            if (!out.io.ok) return out;
            out.content = CsvIO::DecodeText(raw, out.cp1252);
            raw.clear();
            out.legacy = CsvIO::IsLegacyFormat(out.content);
            if (!out.legacy) {
                out.parsed = CsvIO::Parse(out.content, year, &rules);
                out.content.clear();
            }
            return out;
        },
        [this](CsvLoad& r) {
            if (!r.io.ok) {
                SetStatus(StatusLevel::Error, r.io.message.empty() ? "Could not read the CSV file." : r.io.message);
                return;
            }
            importCp1252 = r.cp1252;
            if (r.legacy) {
                importContent = std::move(r.content);
                importYear = Date::Today().year;
                importStage = ImportStage::AskYear;
                openImportPopup = true;
                return;
            }
            ApplyParsedImport(std::move(r.parsed));
        });
}

void App::ContinueImport(int legacyYear) {
    ApplyParsedImport(CsvIO::Parse(importContent, legacyYear, &categorizer));
    importContent.clear();
}

void App::ApplyParsedImport(CsvIO::ParseResult&& p) {
    importDrafts = std::move(p.drafts);
    importReasons = std::move(p.reasons);
    importSkipped = p.skipped;
    importAutoCategorized = p.autoCategorized;
    if (importCp1252) importReasons.insert(importReasons.begin(), "The file was not UTF-8; it was read as Windows-1252.");

    if (importDrafts.empty()) {
        importStage = ImportStage::None;
        SetStatus(StatusLevel::Error,
                  "No valid rows found in " + importFileName + " (" + std::to_string(importSkipped) + " skipped). See Details.",
                  importReasons);
        return;
    }
    importDuplicates = tracker.FindLikelyDuplicates(importDrafts).size();
    if (importDuplicates > 0) {
        importStage = ImportStage::AskDuplicates;
        openImportPopup = true;
        return;
    }
    CommitImport(false);
}

void App::CommitImport(bool skipDuplicates) {
    importStage = ImportStage::None;
    Queue([this, skipDuplicates]() {
        if (!MutationsAllowed()) return;
        ImportResult res = tracker.ImportExpenses(importDrafts, skipDuplicates);
        commands.Clear();   // imports are not undoable (G10)
        std::string msg = "Imported " + std::to_string(res.imported) + " transaction(s) from " + importFileName + ".";
        int skipped = importSkipped + res.invalid;
        if (skipped > 0) msg += " " + std::to_string(skipped) + " invalid row(s) skipped.";
        if (res.duplicatesSkipped > 0) msg += " " + std::to_string(res.duplicatesSkipped) + " duplicate(s) skipped.";
        if (importAutoCategorized > 0)
            msg += " " + std::to_string(importAutoCategorized) + " uncategorized row(s) were categorized from your category rules.";
        SetStatus(skipped > 0 ? StatusLevel::Warning : StatusLevel::Success, msg, importReasons);
        importDrafts.clear();
        importContent.clear();
        if (res.imported > 0) OfferSubscriptionDetection(false);
    });
}

void App::OfferSubscriptionDetection(bool userInitiated) {
    if (!MutationsAllowed()) return;
    SubscriptionScan scan = DetectSubscriptions(tracker.GetExpenses(), tracker.GetRecurringRules(), Date::Today());
    if (scan.found.empty()) {
        if (userInitiated) {
            SetStatus(StatusLevel::Info,
                      "No new subscriptions found. Payments need the type Subscription and must repeat at least twice on a regular schedule.",
                      scan.unresolved);
        }
        return;
    }
    detectedSubs = std::move(scan.found);
    detectedSelected.assign(detectedSubs.size(), 1);
    detectedUnresolved = std::move(scan.unresolved);
    openDetectedPopup = true;
}

void App::ExportCsv() {
    fs::path file;
    if (!Ui::SaveFileDialog(L"CSV files (*.csv)\0*.csv\0", L"csv", L"expenses.csv", file)) return;
    IoResult r = CsvIO::Export(tracker, file);
    if (r.ok) SetStatus(StatusLevel::Success, "Exported " + std::to_string(tracker.GetExpenseCount()) + " transaction(s) to " + Paths::ToUtf8(file));
    else SetStatus(StatusLevel::Error, "Export failed: " + r.message);
}

void App::ExportJson() {
    fs::path file;
    if (!Ui::SaveFileDialog(L"JSON files (*.json)\0*.json\0", L"json", L"expenses-export.json", file)) return;
    AtomicWriteOptions opts;
    opts.keepBackup = false;
    IoResult r = WriteFileAtomic(file, JsonIO::Serialize(tracker), opts);
    if (r.ok) SetStatus(StatusLevel::Success, "Exported JSON to " + Paths::ToUtf8(file));
    else SetStatus(StatusLevel::Error, "Export failed: " + r.message);
}

void App::ExportReport(ReportGenerator::Format format) {
    bool html = format == ReportGenerator::Format::Html;
    std::wstring name = L"report-" + std::to_wstring(reportYear) + L"-" + (reportMonth < 10 ? L"0" : L"") +
                        std::to_wstring(reportMonth) + (html ? L".html" : L".md");
    fs::path file;
    bool chosen = html ? Ui::SaveFileDialog(L"HTML report (*.html)\0*.html\0", L"html", name, file)
                       : Ui::SaveFileDialog(L"Markdown report (*.md)\0*.md\0", L"md", name, file);
    if (!chosen) return;
    std::string content = ReportGenerator::Generate(tracker, reportYear, reportMonth, format, Date::Today());
    AtomicWriteOptions opts;
    opts.keepBackup = false;
    IoResult r = WriteFileAtomic(file, content, opts);
    if (r.ok) SetStatus(StatusLevel::Success, "Report saved to " + Paths::ToUtf8(file));
    else SetStatus(StatusLevel::Error, "Report export failed: " + r.message);
}

// ── Frame ─────────────────────────────────────────────────────────

void App::Render() {
    ImGuiIO& io = ImGui::GetIO();
    ctx.today = Date::Today();
    ctx.readOnly = readOnly || saveBlocked;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGuiWindowFlags mainFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (!showWelcome) mainFlags |= ImGuiWindowFlags_MenuBar;
    ImGui::Begin("##MainWindow", nullptr, mainFlags);
    ImGui::PopStyleVar(2);

    if (showWelcome) {
        RenderWelcome();
        RenderModals();
        ImGui::End();
        ApplyPending();
        PollRateSync();
        PollJobs();
        if (statusTimer > 0.0f) statusTimer -= io.DeltaTime;
        return;
    }

    RenderMenuBar();

    const float statusH = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    ImGui::BeginChild("##Content", ImVec2(0, -statusH), ImGuiChildFlags_None);
    if (ImGui::BeginTabBar("##Tabs")) {
        if (ImGui::BeginTabItem("Dashboard"))     { dashboardTab.Render(ctx);     ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Expenses"))      { expensesTab.Render(ctx);      ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Subscriptions")) { subscriptionsTab.Render(ctx); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Budget"))        { budgetTab.Render(ctx);        ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Goals"))         { goalsTab.Render(ctx);         ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Analytics"))     { analyticsTab.Render(ctx);     ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();

    RenderStatusBar();
    RenderModals();
    HandleShortcuts();
    ImGui::End();

    // Apply all queued changes now that no widget holds tracker pointers.
    ApplyPending();

    // Auto-save 60 s after the first unsaved change.
    if (tracker.HasChanges() && !saveBlocked) {
        autoSaveTimer += io.DeltaTime;
        if (autoSaveTimer >= autoSaveInterval) {
            SaveLedger(true);
            autoSaveTimer = 0.0f;
        }
    } else {
        autoSaveTimer = 0.0f;
    }

    PollRateSync();
    PollJobs();
    CheckDayRollover();
    if (statusTimer > 0.0f) statusTimer -= io.DeltaTime;
}

void App::HandleShortcuts() {
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)) Queue([this]() { SaveLedger(false); });

    // Ledger undo/redo never fires while typing or while a dialog is open (P0-E3).
    const bool typing = ImGui::GetIO().WantTextInput;
    const bool dialogOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (typing || dialogOpen || saveBlocked) return;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal)) Queue([this]() { DoUndo(); });
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiInputFlags_RouteGlobal) ||
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal)) {
        Queue([this]() { DoRedo(); });
    }
}

void App::RenderMenuBar() {
    if (!ImGui::BeginMenuBar()) return;
    const bool locked = saveBlocked;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Save", "Ctrl+S", false, !locked)) Queue([this]() { SaveLedger(false); });
        ImGui::Separator();
        if (ImGui::MenuItem("Import CSV...", nullptr, false, !locked)) Queue([this]() { StartImport(); });
        if (ImGui::MenuItem("Restore from Backup...", nullptr, false, jobs.empty())) Queue([this]() { StartRestore(); });
        if (ImGui::MenuItem("Export CSV...")) Queue([this]() { ExportCsv(); });
        if (ImGui::MenuItem("Export Full Backup...")) Queue([this]() { ExportFullBackup(); });
        if (ImGui::MenuItem("Export JSON...")) Queue([this]() { ExportJson(); });
        if (ImGui::MenuItem("Export Monthly Report...")) {
            reportYear = Date::Today().year;
            reportMonth = Date::Today().month;
            openReport = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Open data folder")) Ui::OpenFolder(Paths::DataDir());
        ImGui::Separator();
        if (ImGui::MenuItem("Exit", "Alt+F4")) RequestExit();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        std::string undoLabel = commands.CanUndo() ? "Undo " + commands.PeekUndoName() : "Undo";
        std::string redoLabel = commands.CanRedo() ? "Redo " + commands.PeekRedoName() : "Redo";
        if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, commands.CanUndo() && !locked)) Queue([this]() { DoUndo(); });
        if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, commands.CanRedo() && !locked)) Queue([this]() { DoRedo(); });
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Welcome Screen")) {
            showWelcome = true;
            welcomeHasData = true;
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Theme")) {
            for (int i = 0; i < ThemeManager::kThemeCount; i++) {
                AppTheme t = static_cast<AppTheme>(i);
                if (ImGui::MenuItem(ThemeManager::ThemeLabel(t), nullptr, ThemeManager::GetCurrentTheme() == t)) {
                    requestedTheme = t;
                    themeChangeRequested = true;
                    settings.theme = ThemeManager::ThemeName(t);
                    SaveSettings();
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Tools")) {
        if (ImGui::MenuItem("Exchange Rates...")) openRates = true;
        if (ImGui::MenuItem(rateSync ? "Syncing Exchange Rates..." : "Sync Exchange Rates Now", nullptr, false,
                            !rateSync && !locked)) {
            StartRateSync(true);
        }
        if (ImGui::MenuItem("Sync Rates on Launch", nullptr, settings.syncRatesOnLaunch)) {
            settings.syncRatesOnLaunch = !settings.syncRatesOnLaunch;
            SaveSettings();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Category Rules...")) openRules = true;
        if (ImGui::MenuItem("Find Subscriptions in Past Payments...", nullptr, false, !locked)) {
            Queue([this]() { OfferSubscriptionDetection(true); });
        }
        if (ImGui::MenuItem("Generate Pending Bills Now", nullptr, false, !locked)) {
            Queue([this]() { RequestRecurringGeneration(true); });
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About Expense Tracker Plus Plus...")) openAbout = true;
        ImGui::EndMenu();
    }

    // Base currency selector (Step 2)
    const float u = ImGui::GetFontSize();
    const float comboW = u * 5.5f;
    const float labelW = ImGui::CalcTextSize("Base currency:").x;
    ImGui::SameLine(ImGui::GetWindowWidth() - comboW - labelW - u * 2.0f);
    ImGui::TextDisabled("Base currency:");
    ImGui::SetNextItemWidth(comboW);
    ImGui::BeginDisabled(locked);
    if (ImGui::BeginCombo("##BaseCurrency", tracker.GetBaseCurrency().c_str())) {
        for (auto& [code, rate] : tracker.GetCurrency().GetAllRates()) {
            if (ImGui::Selectable(code.c_str(), code == tracker.GetBaseCurrency()) && code != tracker.GetBaseCurrency()) {
                std::string chosen = code;
                Queue([this, chosen]() {
                    if (!MutationsAllowed()) return;
                    if (tracker.SetBaseCurrency(chosen, true)) {
                        SetStatus(StatusLevel::Success, "Base currency is now " + chosen +
                                                            ". Budgets were converted at the current exchange rates.");
                    } else {
                        SetStatus(StatusLevel::Error, tracker.GetLastError());
                    }
                });
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    ImGui::EndMenuBar();
}

void App::RenderStatusBar() {
    const ThemeTokens& tk = ThemeManager::Tokens();
    ImGui::Separator();
    ImGui::AlignTextToFramePadding();
    if (statusTimer > 0.0f && !statusMessage.empty()) {
        ImVec4 color = statusLevel == StatusLevel::Error   ? tk.danger
                     : statusLevel == StatusLevel::Warning ? tk.warning
                     : statusLevel == StatusLevel::Success ? tk.success
                                                           : tk.text;
        ImGui::TextColored(color, "%s", statusMessage.c_str());
    } else {
        ImGui::Text("%d transactions", tracker.GetExpenseCount());
        ImGui::SameLine();
        if (readOnly) ImGui::TextColored(tk.warning, "| Read-only");
        else if (saveBlocked) ImGui::TextColored(tk.danger, "| Saving disabled");
        else if (tracker.HasChanges()) ImGui::TextColored(tk.warning, "| Unsaved changes (auto-save within a minute)");
        else ImGui::TextColored(tk.muted, "| All changes saved");
    }
    if (!statusDetails.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Details")) openDetails = true;
    }
}

// ── Modals ────────────────────────────────────────────────────────

void App::RenderModals() {
    RenderLoadFailedModal();
    RenderImportModals();
    RenderDetectedSubscriptionsModal();
    RenderBackfillModal();
    RenderRatesModal();
    RenderRulesModal();
    RenderReportModal();
    RenderAboutModal();
    RenderDetailsModal();
    RenderRestoreModal();
    RenderBusyModal();
}

void App::RenderLoadFailedModal() {
    const char* id = "Could not open your data";
    if (loadFailed && jobs.empty() && !pendingRestore && !ImGui::IsPopupOpen(id)) ImGui::OpenPopup(id);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 34, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    const ThemeTokens& tk = ThemeManager::Tokens();
    ImGui::TextColored(tk.danger, "Your data file could not be read, so nothing has been changed.");
    ImGui::Spacing();
    ImGui::TextWrapped("%s", loadFailedMessage.c_str());
    ImGui::Spacing();
    ImGui::TextWrapped("File: %s", Paths::ToUtf8(Paths::LedgerFile()).c_str());
    if (loadBackupOk) ImGui::TextWrapped("A copy was saved to: %s", Paths::ToUtf8(loadBackupPath).c_str());
    ImGui::Spacing();
    const float bw = ImGui::GetFontSize() * 10;
    if (ImGui::Button("Retry", ImVec2(bw, 0))) {
        ImGui::CloseCurrentPopup();
        Queue([this]() { LoadLedger(); });
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!loadBackupOk);
    if (ImGui::Button("Start empty", ImVec2(bw, 0))) {
        ImGui::CloseCurrentPopup();
        Queue([this]() {
            tracker.ResetAll();
            tracker.ClearDirty();
            commands.Clear();
            loadFailed = false;
            saveBlocked = false;
            saveBlockedReason.clear();
            loadedVersion = JsonIO::kSchemaVersion;
            SetStatus(StatusLevel::Warning, "Started with an empty ledger. Your old file is kept at " + Paths::ToUtf8(loadBackupPath));
        });
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Quit", ImVec2(bw, 0))) RequestExit();
    if (ImGui::Button("Restore from a backup...", ImVec2(bw * 3 + ImGui::GetStyle().ItemSpacing.x * 2, 0))) {
        ImGui::CloseCurrentPopup();
        Queue([this]() { StartRestore(); });
    }
    if (!loadBackupOk) Ui::MutedText("\"Start empty\" is disabled because no backup copy could be made.");
    ImGui::EndPopup();
}

void App::RenderImportModals() {
    const float u = ImGui::GetFontSize();
    if (openImportPopup) {
        ImGui::OpenPopup(importStage == ImportStage::AskYear ? "Import legacy CSV" : "Possible duplicates");
        openImportPopup = false;
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Import legacy CSV", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s uses the old format with a Month column but no dates.", importFileName.c_str());
        ImGui::Text("Which year do these transactions belong to?");
        Ui::YearInput("Year", importYear, u * 7);
        Ui::MutedText("Each transaction is dated the 1st of its month.");
        if (ImGui::Button("Continue", ImVec2(u * 7, 0))) {
            ImGui::CloseCurrentPopup();
            int year = importYear;
            Queue([this, year]() { ContinueImport(year); });
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(u * 7, 0))) {
            importStage = ImportStage::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Possible duplicates", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%d of %d row(s) in %s match transactions you already have", static_cast<int>(importDuplicates),
                    static_cast<int>(importDrafts.size()), importFileName.c_str());
        Ui::MutedText("(same date, description, amount, currency and type).");
        ImGui::Spacing();
        if (ImGui::Button("Skip duplicates", ImVec2(u * 9, 0))) {
            ImGui::CloseCurrentPopup();
            CommitImport(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Import all", ImVec2(u * 9, 0))) {
            ImGui::CloseCurrentPopup();
            CommitImport(false);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(u * 7, 0))) {
            importStage = ImportStage::None;
            importDrafts.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void App::RenderDetectedSubscriptionsModal() {
    const char* id = "Subscriptions found";
    if (openDetectedPopup) {
        ImGui::OpenPopup(id);
        openDetectedPopup = false;
    }
    const float u = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(u * 52, u * 30), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(id, nullptr)) return;

    ImGui::TextWrapped("These payments repeat on a regular schedule. Create them as subscriptions so the Subscriptions "
                       "tab shows them, with renewal reminders and your monthly burn rate.");
    Ui::MutedText("Past payments are linked to the new subscriptions and are never recorded twice.");
    ImGui::Spacing();

    int selected = 0;
    for (char c : detectedSelected) selected += c ? 1 : 0;
    const float listH = detectedUnresolved.empty() ? -ImGui::GetFrameHeightWithSpacing() * 1.5f : -u * 8.0f;
    if (ImGui::BeginTable("##detected", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0, listH))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, u * 1.6f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Amount", ImGuiTableColumnFlags_WidthFixed, u * 7);
        ImGui::TableSetupColumn("Renews", ImGuiTableColumnFlags_WidthFixed, u * 7);
        ImGui::TableSetupColumn("Payments", ImGuiTableColumnFlags_WidthFixed, u * 4.5f);
        ImGui::TableSetupColumn("Next renewal", ImGuiTableColumnFlags_WidthFixed, u * 10);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < detectedSubs.size(); i++) {
            const DetectedSubscription& d = detectedSubs[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            bool on = detectedSelected[i] != 0;
            if (ImGui::Checkbox("##on", &on)) detectedSelected[i] = on ? 1 : 0;
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(d.description.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(MoneyUtil::Format(d.amount, d.currency).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(d.ToRule().CycleLabel().c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%d", static_cast<int>(d.expenseIds.size()));
            ImGui::TableNextColumn();
            if (d.looksStopped) Ui::MutedText("stopped? (paused)");
            else ImGui::TextUnformatted(d.nextRenewal.ToString().c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (!detectedUnresolved.empty()) {
        ImGui::Text("Not created automatically (%d):", static_cast<int>(detectedUnresolved.size()));
        ImGui::BeginChild("##unresolved", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.2f), ImGuiChildFlags_Borders);
        for (auto& line : detectedUnresolved) Ui::MutedText("%s", line.c_str());
        ImGui::EndChild();
        Ui::MutedText("If those renew, add them yourself with + Add Subscription.");
    }

    ImGui::BeginDisabled(selected == 0);
    std::string createLabel = "Create " + std::to_string(selected) + " subscription(s)";
    if (ImGui::Button(createLabel.c_str())) {
        std::vector<DetectedSubscription> chosen;
        for (size_t i = 0; i < detectedSubs.size(); i++) {
            if (detectedSelected[i]) chosen.push_back(detectedSubs[i]);
        }
        ImGui::CloseCurrentPopup();
        Queue([this, chosen]() {
            if (!MutationsAllowed()) return;
            int n = tracker.AddDetectedSubscriptions(chosen);
            commands.Clear();   // linking edits transactions; older undo steps would no longer match
            if (n > 0) {
                SetStatus(StatusLevel::Success,
                          "Created " + std::to_string(n) + " subscription(s) from past payments. See the Subscriptions tab.");
            } else {
                SetStatus(StatusLevel::Error, "Could not create the subscriptions: " + tracker.GetLastError());
            }
            RequestRecurringGeneration(false);
        });
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Select all")) detectedSelected.assign(detectedSubs.size(), 1);
    ImGui::SameLine();
    if (ImGui::Button("Not now", ImVec2(u * 7, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void App::RenderBackfillModal() {
    if (openBackfill) {
        ImGui::OpenPopup("Generate past bills?");
        openBackfill = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Generate past bills?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    const float u = ImGui::GetFontSize();
    ImGui::Text("Your subscriptions have %d past occurrence(s) that were never recorded", pendingBackfill.total);
    ImGui::Text("(up to %d for a single subscription).", pendingBackfill.maxPerRule);
    ImGui::Spacing();
    if (ImGui::Button("Generate all", ImVec2(u * 9, 0))) {
        ImGui::CloseCurrentPopup();
        Queue([this]() { DoGenerate(false, true); });
    }
    ImGui::SameLine();
    if (ImGui::Button("Start from today", ImVec2(u * 9, 0))) {
        ImGui::CloseCurrentPopup();
        Queue([this]() { DoGenerate(true, true); });
    }
    ImGui::SameLine();
    if (ImGui::Button("Not now", ImVec2(u * 7, 0))) {
        backfillSuppressed = true;
        ImGui::CloseCurrentPopup();
    }
    Ui::MutedText("\"Start from today\" skips the history and only records bills from today onward.");
    ImGui::EndPopup();
}

void App::RenderRatesModal() {
    if (openRates) {
        ImGui::OpenPopup("Exchange Rates");
        openRates = false;
        ratesError.clear();
    }
    const float u = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(u * 30, u * 28), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Exchange Rates", nullptr)) return;

    const CurrencyManager& cm = tracker.GetCurrency();
    const std::string& base = cm.GetBaseCurrency();
    ImGui::TextWrapped("Values are how much one unit of each currency is worth in the base currency, %s. "
                       "Without internet the last synced or edited rates are used.", base.c_str());
    std::string provenance = cm.GetSource().empty()   ? "Using built-in approximate defaults."
                           : cm.GetSource() == "manual" ? "Last edited manually on " + cm.GetAsOf() + "."
                           : "Synced from " + cm.GetSource() + (cm.GetAsOf().empty() ? "" : ", rates as of " + cm.GetAsOf()) + ".";
    Ui::MutedText("%s", provenance.c_str());
    ImGui::BeginDisabled(rateSync != nullptr || saveBlocked);
    if (ImGui::Button(rateSync ? "Syncing..." : "Sync now")) StartRateSync(true);
    ImGui::EndDisabled();
    ImGui::SameLine();
    Ui::MutedText(settings.syncRatesOnLaunch ? "Rates sync automatically at launch; manual edits last until the next sync."
                                             : "Automatic sync is off (Tools > Sync Rates on Launch).");

    ImGui::BeginDisabled(saveBlocked);
    if (ImGui::BeginTable("##Rates", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
                          ImVec2(0, ImGui::GetContentRegionAvail().y - u * 5))) {
        ImGui::TableSetupColumn("Currency");
        ImGui::TableSetupColumn(("1 unit in " + base).c_str(), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("");
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (auto& [code, pivotRate] : cm.GetAllRates()) {
            ImGui::TableNextRow();
            ImGui::PushID(code.c_str());
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(code.c_str());
            ImGui::TableSetColumnIndex(1);
            if (code == base) {
                ImGui::TextDisabled("1 (base currency)");
            } else {
                double committed;
                double current = cm.GetRateInBase(code).value_or(0.0);
                if (Ui::CommitDoubleInput("##rate", current, committed, -FLT_MIN, "%.6g")) {
                    std::string c = code;
                    Queue([this, c, committed]() {
                        if (!MutationsAllowed()) return;
                        if (!tracker.SetExchangeRateInBase(c, committed)) SetStatus(StatusLevel::Error, tracker.GetLastError());
                    });
                }
            }
            ImGui::TableSetColumnIndex(2);
            if (code != base && code != CurrencyManager::kPivot && ImGui::SmallButton("Remove")) {
                std::string c = code;
                Queue([this, c]() {
                    if (!MutationsAllowed()) return;
                    if (!tracker.RemoveExchangeRate(c)) SetStatus(StatusLevel::Error, tracker.GetLastError());
                });
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::SetNextItemWidth(u * 5);
    ImGui::InputTextWithHint("##newcode", "Code", &newRateCode, ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(u * 8);
    ImGui::InputDouble("##newrate", &newRateValue, 0.0, 0.0, "%.6g");
    ImGui::SameLine();
    if (ImGui::Button("Add / update")) {
        std::string code;
        ratesError.clear();
        if (!Validation::NormalizeCurrency(newRateCode, code)) {
            ratesError = "Enter a 3-letter currency code.";
        } else if (code == base) {
            ratesError = "The base currency is always 1.";
        } else if (!(newRateValue > 0.0)) {
            ratesError = "Enter a rate greater than zero.";
        } else {
            double v = newRateValue;
            Queue([this, code, v]() {
                if (!MutationsAllowed()) return;
                if (!tracker.SetExchangeRateInBase(code, v)) SetStatus(StatusLevel::Error, tracker.GetLastError());
            });
            newRateCode.clear();
            newRateValue = 0.0;
        }
    }
    ImGui::EndDisabled();
    Ui::ErrorText(ratesError);
    if (ImGui::Button("Close", ImVec2(u * 7, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void App::RenderRulesModal() {
    if (openRules) {
        ImGui::OpenPopup("Category Rules");
        openRules = false;
        rulesError.clear();
    }
    const float u = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(u * 30, u * 30), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Category Rules", nullptr)) return;

    ImGui::TextWrapped("When a description contains a keyword (as a whole word), the category is suggested "
                       "automatically. The longest matching keyword wins.");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "Filter keywords or categories", &rulesFilter);

    std::string toRemove;
    if (ImGui::BeginTable("##RulesTable", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
                          ImVec2(0, ImGui::GetContentRegionAvail().y - u * 5))) {
        ImGui::TableSetupColumn("Keyword", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("");
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (auto& [keyword, category] : categorizer.GetRules()) {
            if (!rulesFilter.empty() && !Utils::ContainsCI(keyword, rulesFilter) && !Utils::ContainsCI(category, rulesFilter)) continue;
            ImGui::TableNextRow();
            ImGui::PushID(keyword.c_str());
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(keyword.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(category.c_str());
            ImGui::TableSetColumnIndex(2);
            if (ImGui::SmallButton("Remove")) toRemove = keyword;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (!toRemove.empty()) {
        categorizer.RemoveRule(toRemove);
        SaveCategorizer();
    }

    ImGui::SetNextItemWidth(u * 9);
    ImGui::InputTextWithHint("##kw", "keyword", &newRuleKeyword);
    ImGui::SameLine();
    Ui::CategoryInput("##cat", newRuleCategory, tracker.GetCategories(), u * 10);
    ImGui::SameLine();
    if (ImGui::Button("Add rule")) {
        rulesError.clear();
        if (categorizer.AddRule(newRuleKeyword, newRuleCategory, rulesError)) {
            SaveCategorizer();
            newRuleKeyword.clear();
            newRuleCategory.clear();
        }
    }
    Ui::ErrorText(rulesError);
    if (ImGui::Button("Close", ImVec2(u * 7, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    if (ImGui::Button("Reset to defaults")) {
        categorizer.ResetToDefaults();
        SaveCategorizer();
    }
    ImGui::EndPopup();
}

void App::RenderReportModal() {
    if (openReport) {
        ImGui::OpenPopup("Export Monthly Report");
        openReport = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Export Monthly Report", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    const float u = ImGui::GetFontSize();
    Ui::YearInput("Year", reportYear, u * 7);
    Ui::MonthCombo("Month", reportMonth, false, u * 9);
    Ui::MutedText("Includes the cash-flow summary, budget adherence, top 10 transactions and active subscriptions.");
    ImGui::Spacing();
    if (ImGui::Button("Export HTML...", ImVec2(u * 9, 0))) {
        ImGui::CloseCurrentPopup();
        Queue([this]() { ExportReport(ReportGenerator::Format::Html); });
    }
    ImGui::SameLine();
    if (ImGui::Button("Export Markdown...", ImVec2(u * 9, 0))) {
        ImGui::CloseCurrentPopup();
        Queue([this]() { ExportReport(ReportGenerator::Format::Markdown); });
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(u * 7, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void App::RenderAboutModal() {
    const char* id = "About Expense Tracker Plus Plus";
    if (openAbout) {
        if (aboutNotices.empty()) aboutNotices = Ui::EmbeddedNotices();
        ImGui::OpenPopup(id);
        openAbout = false;
    }
    const float u = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(u * 46, u * 34), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(id, nullptr)) return;

    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.5f);
    ImGui::TextUnformatted(AppInfo::kName);
    ImGui::PopFont();
    ImGui::Text("Version %s", AppInfo::kVersion);
    Ui::MutedText("%s", AppInfo::kTagline);
    ImGui::Text("By %s. Released under the %s.", AppInfo::kPublisher, AppInfo::kLicense);
    ImGui::Spacing();
    ImGui::TextLinkOpenURL("Project page on GitHub", AppInfo::kRepoUrl);
    ImGui::SameLine();
    ImGui::TextLinkOpenURL("Report a problem or suggest a feature", AppInfo::kIssuesUrl);
    ImGui::SameLine();
    ImGui::TextLinkOpenURL("Privacy", AppInfo::kPrivacyUrl);
    ImGui::Spacing();
    ImGui::TextUnformatted("Your data stays on this computer. The only network request is the exchange-rate download.");
    ImGui::TextUnformatted("Rates by ");
    ImGui::SameLine(0, 0);
    ImGui::TextLinkOpenURL("ExchangeRate-API", AppInfo::kRatesUrl);
    ImGui::Spacing();
    ImGui::SeparatorText("Third-party software and licences");
    ImGui::BeginChild("##notices", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(aboutNotices.c_str(), aboutNotices.c_str() + aboutNotices.size());
    ImGui::EndChild();
    if (ImGui::Button("Close", ImVec2(u * 7, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void App::RenderDetailsModal() {
    if (openDetails) {
        ImGui::OpenPopup("Details");
        openDetails = false;
    }
    const float u = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(u * 36, u * 22), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Details", nullptr)) return;
    ImGui::BeginChild("##detailsList", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_Borders);
    for (auto& line : statusDetails) ImGui::BulletText("%s", line.c_str());
    ImGui::EndChild();
    if (ImGui::Button("Close", ImVec2(u * 7, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ── Background jobs ───────────────────────────────────────────────

void App::PollJobs() {
    for (size_t i = 0; i < jobs.size();) {
        if (jobs[i].poll()) jobs.erase(jobs.begin() + static_cast<std::ptrdiff_t>(i));
        else i++;
    }
}

void App::RenderBusyModal() {
    const char* id = "Working...";
    if (!jobs.empty() && !ImGui::IsPopupOpen(id)) ImGui::OpenPopup(id);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) return;
    if (jobs.empty()) {
        ImGui::CloseCurrentPopup();
    } else {
        const Job& job = jobs.front();
        double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - job.started).count();
        const char* spinner = "|/-\\";
        ImGui::Text("%c  %s", spinner[static_cast<int>(secs * 8.0) % 4], job.label.c_str());
        Ui::MutedText("%.0f s elapsed. Large files can take a little while.", secs);
    }
    ImGui::EndPopup();
}

// ── Restore from backup ───────────────────────────────────────────

void App::StartRestore() {
    if (!jobs.empty()) return;
    fs::path file;
    if (!Ui::OpenFileDialog(L"Backups and exports (*.json;*.bak)\0*.json;*.bak\0All files (*.*)\0*.*\0", file,
                            Paths::DataDir())) {
        return;
    }
    RunJob<RestoreLoad>("Reading backup " + Paths::ToUtf8(file.filename()) + "...",
        [file]() {
            RestoreLoad out;
            out.file = file;
            std::error_code ec;
            out.bytes = fs::file_size(file, ec);
            const auto start = std::chrono::steady_clock::now();
            out.io = JsonIO::LoadBackupFile(file, out.data, out.report, out.extras);
            out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            return out;
        },
        [this](RestoreLoad& r) {
            if (!r.io.ok) {
                SetStatus(StatusLevel::Error, "Could not read " + Paths::ToUtf8(r.file.filename()) + ": " +
                                                  (r.io.message.empty() ? std::string("unknown error") : r.io.message));
                return;
            }
            pendingRestore = std::make_unique<RestoreLoad>(std::move(r));
            openRestorePopup = true;
        });
}

void App::DoRestore(bool replace) {
    if (!pendingRestore) return;
    std::unique_ptr<RestoreLoad> restore = std::move(pendingRestore);
    const std::string name = Paths::ToUtf8(restore->file.filename());
    const fs::path ledger = Paths::LedgerFile();
    std::error_code ec;

    // 1. Make sure unsaved work is on disk, then copy the current file aside.
    if (tracker.HasChanges() && !saveBlocked) SaveLedger(true);
    fs::path before;
    if (fs::exists(ledger, ec)) {
        before = Paths::TimestampedSibling(ledger, "before-restore");
        IoResult b = CopyFileSafe(ledger, before);
        if (!b.ok) {
            SetStatus(StatusLevel::Error, "Restore cancelled: the current data could not be backed up. " + b.message);
            return;
        }
    }

    // 2. Apply.
    std::string msg;
    if (replace) {
        tracker.ReplaceAll(std::move(restore->data));
        tracker.MarkDirty();
        msg = "Restored " + std::to_string(tracker.GetExpenseCount()) + " transaction(s) from " + name + ".";
    } else {
        MergeResult m = tracker.MergeLedger(restore->data);
        msg = "Merged " + name + ": " + std::to_string(m.expensesAdded) + " transaction(s) added";
        if (m.expensesDuplicate) msg += ", " + std::to_string(m.expensesDuplicate) + " duplicate(s) skipped";
        if (m.rulesAdded) msg += ", " + std::to_string(m.rulesAdded) + " subscription(s)";
        if (m.goalsAdded) msg += ", " + std::to_string(m.goalsAdded) + " goal(s)";
        if (m.budgetsAdded) msg += ", " + std::to_string(m.budgetsAdded) + " budget(s)";
        msg += ".";
    }

    // A full backup also carries settings and category rules.
    const JsonIO::BackupExtras& extras = restore->extras;
    if (extras.hasRules) {
        if (replace) {
            int n = categorizer.ReplaceRules(extras.rules);
            if (n > 0) msg += " " + std::to_string(n) + " category rule(s) restored.";
        } else {
            int n = categorizer.MergeRules(extras.rules);
            if (n > 0) msg += " " + std::to_string(n) + " category rule(s) added.";
        }
        SaveCategorizer();
    }
    if (extras.hasSettings && replace) {
        if (!extras.theme.empty()) {
            requestedTheme = ThemeManager::ThemeFromName(extras.theme);
            themeChangeRequested = true;
            settings.theme = ThemeManager::ThemeName(requestedTheme);
        }
        settings.syncRatesOnLaunch = extras.syncRatesOnLaunch;
        msg += " Settings restored.";
    }
    if (restoreFromWelcome) {
        settings.welcomeCompleted = true;
        showWelcome = false;
        restoreFromWelcome = false;
    }
    SaveSettings();

    // A successful restore also resolves an unreadable or newer-version data file.
    loadFailed = false;
    saveBlocked = false;
    readOnly = false;
    saveBlockedReason.clear();
    loadedVersion = JsonIO::kSchemaVersion;
    versionBackupDone = true;
    commands.Clear();   // undo history refers to the old data

    if (SaveLedger(true)) {
        if (!before.empty()) msg += " Previous data saved as " + Paths::ToUtf8(before.filename()) + ".";
        SetStatus(restore->report.TotalSkipped() > 0 ? StatusLevel::Warning : StatusLevel::Success, msg,
                  restore->report.messages);
    }
    backfillSuppressed = false;
    RequestRecurringGeneration(false);
}

void App::RenderRestoreModal() {
    const char* id = "Restore from Backup";
    if (openRestorePopup && jobs.empty()) {
        ImGui::OpenPopup(id);
        openRestorePopup = false;
    }
    const float u = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(u * 34, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    if (!pendingRestore) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const ThemeTokens& tk = ThemeManager::Tokens();
    const RestoreLoad& r = *pendingRestore;
    const LedgerSummary s = SummarizeLedger(r.data);

    char size[64];
    double mb = static_cast<double>(r.bytes) / (1024.0 * 1024.0);
    if (mb >= 1.0) std::snprintf(size, sizeof(size), "%.1f MB", mb);
    else std::snprintf(size, sizeof(size), "%.0f KB", static_cast<double>(r.bytes) / 1024.0);
    ImGui::Text("File: %s  (%s, read in %.1f s)", Paths::ToUtf8(r.file.filename()).c_str(), size, r.seconds);
    Ui::MutedText("%s", Paths::ToUtf8(r.file.parent_path()).c_str());
    ImGui::Separator();

    int total = s.expenses + s.incomes + s.transfers;
    ImGui::Text("%d transaction(s): %d expense(s), %d income, %d transfer(s)", total, s.expenses, s.incomes, s.transfers);
    if (s.hasDates) ImGui::Text("Dates: %s to %s", s.firstDate.ToString().c_str(), s.lastDate.ToString().c_str());
    ImGui::Text("%d subscription(s), %d goal(s), budgets for %d month(s)", s.rules, s.goals, s.budgetMonths);
    ImGui::Text("Base currency: %s   File format: version %d", s.baseCurrency.c_str(), r.report.fileVersion);
    if (r.extras.isFullBackup) {
        ImGui::TextColored(tk.success, "Full backup%s%s: includes settings and %d category rule(s).",
                           r.extras.createdAt.empty() ? "" : " from ", r.extras.createdAt.c_str(),
                           static_cast<int>(r.extras.rules.size()));
    } else {
        Ui::MutedText("Ledger file (settings and category rules on this computer are kept).");
    }
    if (r.report.TotalSkipped() > 0) {
        ImGui::TextColored(tk.warning, "%d invalid record(s) will be skipped.", r.report.TotalSkipped());
        if (ImGui::TreeNode("Show skipped records")) {
            ImGui::BeginChild("##skipped", ImVec2(u * 32, u * 8), ImGuiChildFlags_Borders);
            for (auto& line : r.report.messages) ImGui::BulletText("%s", line.c_str());
            ImGui::EndChild();
            ImGui::TreePop();
        }
    }

    const bool newer = r.report.newerVersion;
    const bool canMerge = !newer && !readOnly;
    if (newer) {
        ImGui::TextColored(tk.danger, "This backup was written by a newer version of the app and cannot be restored here.");
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(tk.text, "Replace");
    ImGui::SameLine();
    Ui::MutedText("- your data becomes exactly this backup.");
    ImGui::TextColored(tk.text, "Merge");
    ImGui::SameLine();
    Ui::MutedText("- adds what is missing; duplicates are skipped; your budgets and rates are kept.");
    Ui::MutedText("Either way, your current data file is copied to expenses.before-restore-<date>.json first.");
    if (!canMerge && !newer) Ui::MutedText("Merge is unavailable while the current data is open read-only.");
    ImGui::Spacing();

    const float bw = u * 9;
    if (restoreFromWelcome && !welcomeHasData) {
        ImGui::BeginDisabled(newer);
        if (ImGui::Button("Restore everything", ImVec2(bw * 1.4f, 0))) {
            ImGui::CloseCurrentPopup();
            Queue([this]() { DoRestore(true); });
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(u * 6, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            pendingRestore.reset();
            restoreFromWelcome = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return;
    }
    ImGui::BeginDisabled(newer);
    if (Ui::DangerButton("Replace current data", ImVec2(bw * 1.3f, 0))) {
        ImGui::CloseCurrentPopup();
        Queue([this]() { DoRestore(true); });
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!canMerge);
    if (ImGui::Button("Merge into current data", ImVec2(bw * 1.3f, 0))) {
        ImGui::CloseCurrentPopup();
        Queue([this]() { DoRestore(false); });
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(u * 6, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        pendingRestore.reset();
        restoreFromWelcome = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ── Welcome screen & full backup ──────────────────────────────────

void App::ExportFullBackup() {
    fs::path file;
    const Date today = Date::Today();
    std::wstring name = L"ExpenseTrackerPlusPlus-Backup-" + Paths::FromUtf8(today.ToString()).wstring() + L".json";
    if (!Ui::SaveFileDialog(L"Expense Tracker Plus Plus backup (*.json)\0*.json\0", L"json", name, file)) return;
    IoResult r = JsonIO::SaveFullBackup(tracker, settings, categorizer, today, file);
    if (r.ok) {
        SetStatus(StatusLevel::Success, "Full backup saved to " + Paths::ToUtf8(file) +
                                            ". Copy this one file to your new computer and choose \"I have a backup\".");
    } else {
        SetStatus(StatusLevel::Error, "Backup failed: " + r.message);
    }
}

void App::FinishWelcome() {
    showWelcome = false;
    settings.welcomeCompleted = true;
    SaveSettings();
}

void App::RenderWelcome() {
    // Keep the status line for errors (e.g. an unreadable backup file).
    const float statusH = statusTimer > 0.0f && !statusMessage.empty() ? ImGui::GetFrameHeightWithSpacing() : 0.0f;
    ImGui::BeginChild("##WelcomeArea", ImVec2(0, -statusH), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    WelcomeScreen::Action action = welcome.Render(welcomeHasData);
    ImGui::EndChild();
    if (statusH > 0.0f) RenderStatusBar();

    switch (action) {
        case WelcomeScreen::Action::FreshStart:
            if (welcomeHasData) {
                openFreshConfirm = true;
            } else {
                Queue([this]() {
                    FinishWelcome();
                    tracker.MarkDirty();
                    SaveLedger(true);
                    SetStatus(StatusLevel::Success,
                              "Welcome! Add your first transaction from the Expenses tab, or set up subscriptions and budgets.");
                });
            }
            break;
        case WelcomeScreen::Action::RestoreBackup:
            restoreFromWelcome = true;
            Queue([this]() { StartRestore(); });
            break;
        case WelcomeScreen::Action::Close:
            showWelcome = false;
            break;
        case WelcomeScreen::Action::None:
            break;
    }

    // Fresh start while data exists (opened from View > Welcome Screen).
    if (openFreshConfirm) {
        ImGui::OpenPopup("Start fresh?");
        openFreshConfirm = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Start fresh?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const float u = ImGui::GetFontSize();
        ImGui::Text("This starts an empty ledger. Your current %d transaction(s) are backed up first.",
                    tracker.GetExpenseCount());
        Ui::MutedText("You can bring them back any time with File > Restore from Backup...");
        ImGui::Spacing();
        if (Ui::DangerButton("Start fresh", ImVec2(u * 8, 0))) {
            ImGui::CloseCurrentPopup();
            Queue([this]() {
                DoReset();
                FinishWelcome();
            });
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(u * 6, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
