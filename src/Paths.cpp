#include "Paths.h"
#include "AtomicFile.h"
#include <cstdlib>
#include <ctime>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#endif

namespace fs = std::filesystem;

namespace Paths {

static fs::path g_dataDir;

std::string ToUtf8(const fs::path& p) {
    return p.u8string();
}

fs::path FromUtf8(const std::string& s) {
    return fs::u8path(s);
}

fs::path ExecutableDir() {
#ifdef _WIN32
    std::wstring buf(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n > 0 && n < buf.size()) {
        buf.resize(n);
        return fs::path(buf).parent_path();
    }
#endif
    std::error_code ec;
    return fs::current_path(ec);
}

// %APPDATA% (roaming), or empty when unavailable.
static fs::path RoamingAppData() {
#ifdef _WIN32
    PWSTR roaming = nullptr;
    fs::path p;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming)) && roaming) p = fs::path(roaming);
    if (roaming) CoTaskMemFree(roaming);
    return p;
#else
    return fs::path();
#endif
}

// Documents (for the Microsoft Store package), or empty when unavailable.
static fs::path DocumentsDir() {
#ifdef _WIN32
    PWSTR docs = nullptr;
    fs::path p;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)) && docs) p = fs::path(docs);
    if (docs) CoTaskMemFree(docs);
    return p;
#else
    return fs::path();
#endif
}

bool IsPackaged() {
#ifdef _WIN32
    // GetCurrentPackageFamilyName is looked up at run time so the exe still starts on any Windows.
    using Fn = LONG(WINAPI*)(UINT32*, PWSTR);
    static const Fn fn = reinterpret_cast<Fn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetCurrentPackageFamilyName")));
    if (!fn) return false;
    UINT32 len = 0;
    return fn(&len, nullptr) != APPMODEL_ERROR_NO_PACKAGE;
#else
    return false;
#endif
}

// True when EXPENSE_TRACKER_DATA_DIR points the app at another folder (tests, portable use).
static bool DataDirOverridden(fs::path* out = nullptr) {
#ifdef _WIN32
    std::wstring overrideDir(32768, L' ');
    DWORD len = GetEnvironmentVariableW(L"EXPENSE_TRACKER_DATA_DIR", overrideDir.data(),
                                        static_cast<DWORD>(overrideDir.size()));
    if (len > 0 && len < overrideDir.size()) {
        overrideDir.resize(len);
        if (out) *out = fs::path(overrideDir);
        return true;
    }
#else
    if (const char* overrideDir = std::getenv("EXPENSE_TRACKER_DATA_DIR")) {
        if (*overrideDir) {
            if (out) *out = fs::path(overrideDir);
            return true;
        }
    }
#endif
    return false;
}

static fs::path DefaultDataDir() {
    fs::path overridden;
    if (DataDirOverridden(&overridden)) return overridden;
    // The Microsoft Store (MSIX) build keeps data in Documents: inside AppData, Windows would hide
    // new files in a private per-package folder that is deleted when the app is uninstalled.
    if (IsPackaged()) {
        fs::path docs = DocumentsDir();
        if (!docs.empty()) return docs / L"Expense Tracker Plus Plus";
    }
    fs::path roaming = RoamingAppData();
    if (!roaming.empty()) return roaming / L"ExpenseTrackerPlusPlus";
    return ExecutableDir() / "data";
}

void Init() {
    if (g_dataDir.empty()) g_dataDir = DefaultDataDir();
}

void SetDataDirForTesting(const fs::path& dir) {
    g_dataDir = dir;
}

fs::path DataDir() {
    Init();
    return g_dataDir;
}

fs::path LedgerFile()        { return DataDir() / "expenses.json"; }
fs::path SettingsFile()      { return DataDir() / "settings.json"; }
fs::path CategoryRulesFile() { return DataDir() / "category_rules.json"; }

std::string MigrateLegacyLocations() {
    std::error_code ec;
    if (fs::exists(LedgerFile(), ec)) return "";

    std::vector<fs::path> candidates;
    // The app was called "Expense Tracker Plus" before; its data folder is copied
    // once (never for a test/portable override, which must stay isolated).
    if (!DataDirOverridden() && !RoamingAppData().empty()) {
        // The Store build starts from the data of the downloaded version (same PC, same user).
        if (IsPackaged()) candidates.push_back(RoamingAppData() / L"ExpenseTrackerPlusPlus");
        candidates.push_back(RoamingAppData() / L"ExpenseTrackerPlus");
    }
    candidates.push_back(fs::current_path(ec) / "data");
    candidates.push_back(ExecutableDir() / "data");

    for (auto& dir : candidates) {
        fs::path legacy = dir / "expenses.json";
        if (!fs::exists(legacy, ec) || fs::equivalent(dir, DataDir(), ec)) continue;
        IoResult r = CopyFileSafe(legacy, LedgerFile());
        if (!r.ok) return "Found data at " + ToUtf8(legacy) + " but could not migrate it: " + r.message;
        for (const char* extra : {"category_rules.json", "settings.json"}) {
            if (fs::exists(dir / extra, ec) && !fs::exists(DataDir() / extra, ec)) {
                CopyFileSafe(dir / extra, DataDir() / extra);
            }
        }
        return "Migrated existing data from " + ToUtf8(dir) + " to " + ToUtf8(DataDir()) +
               " (the original was left in place).";
    }
    return "";
}

fs::path TimestampedSibling(const fs::path& file, const std::string& tag) {
    std::time_t t = std::time(nullptr);
    char stamp[32] = "00000000-000000";
    if (std::tm* tm = std::localtime(&t)) std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", tm);
    fs::path name = file.stem();
    name += "." + tag + "-" + stamp;
    name += file.extension();
    return file.parent_path() / name;
}

} // namespace Paths
