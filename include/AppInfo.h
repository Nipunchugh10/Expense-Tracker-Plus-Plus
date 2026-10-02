#pragma once
// Single source of truth for the application's identity. Included by the C++ code AND by
// resources/app.rc (the Windows version info), so a release only edits this file.
// Bump ETP_VERSION_STRING and ETP_VERSION_COMMA together.

#define ETP_APP_NAME       "Expense Tracker Plus Plus"
#define ETP_VERSION_STRING "2.0.0"
#define ETP_VERSION_COMMA  2,0,0,0
#define ETP_PUBLISHER      "Nipunchugh10"

#ifdef __cplusplus
namespace AppInfo {
    inline constexpr const char* kName        = ETP_APP_NAME;
    inline constexpr const char* kVersion     = ETP_VERSION_STRING;
    inline constexpr const char* kPublisher   = ETP_PUBLISHER;
    inline constexpr const char* kTagline     = "A private, offline-first personal finance tracker for Windows.";
    inline constexpr const char* kLicense     = "MIT License";
    inline constexpr const char* kRepoUrl     = "https://github.com/Nipunchugh10/Expense-Tracker-Plus-Plus";
    inline constexpr const char* kIssuesUrl   = "https://github.com/Nipunchugh10/Expense-Tracker-Plus-Plus/issues";
    inline constexpr const char* kPrivacyUrl  = "https://github.com/Nipunchugh10/Expense-Tracker-Plus-Plus#privacy";
    inline constexpr const char* kRatesUrl    = "https://www.exchangerate-api.com";
}
#endif
