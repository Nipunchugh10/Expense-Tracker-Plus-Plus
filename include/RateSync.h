#pragma once
#include <map>
#include <string>

// Online exchange-rate sync. The only network access in the app: an anonymous
// HTTPS GET for the latest USD-based rates. No user data is sent. It is
// optional (Tools > Sync Rates on Launch) and the app works fully offline.
namespace RateSync {
    struct Result {
        bool ok = false;
        std::string error;
        std::map<std::string, double> pivotRates;   // value of one unit in USD (the ledger's pivot)
        std::string asOf;                           // YYYY-MM-DD reported by the provider
        std::string source;                         // provider host name
    };

    // Response parsers (pure functions, unit tested). Both providers return
    // "1 USD = X units"; they are inverted into pivot rates.
    bool ParseOpenErApi(const std::string& body, Result& out);
    bool ParseFrankfurter(const std::string& body, Result& out);

    // Blocking HTTPS GET via WinHTTP with short timeouts.
    bool HttpGet(const std::wstring& host, const std::wstring& path, std::string& body, std::string& error);

    // Tries the primary provider, then the fallback. Blocking: call it from a
    // worker thread. Never throws.
    Result FetchLatest();
}
