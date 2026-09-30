#include "RateSync.h"
#include "Expense.h"
#include "Utils.h"
#include "Validation.h"
#include <nlohmann/json.hpp>
#include <cmath>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#endif

using json = nlohmann::json;

namespace RateSync {

namespace {

constexpr size_t kMaxBodyBytes = 2 * 1024 * 1024;
constexpr size_t kMinCurrencies = 10;   // a real response has far more; fewer means something is wrong

// Converts {"CODE": units-per-USD} into pivot rates (USD per unit), dropping
// anything that is not a plausible positive number.
bool ReadRates(const json& rates, Result& out) {
    if (!rates.is_object()) return false;
    out.pivotRates.clear();
    for (auto& [code, v] : rates.items()) {
        std::string c;
        if (!Validation::NormalizeCurrency(code, c) || !v.is_number()) continue;
        double perUsd = v.get<double>();
        if (!std::isfinite(perUsd) || perUsd <= 0.0 || perUsd > 1e9) continue;
        out.pivotRates[c] = 1.0 / perUsd;
    }
    out.pivotRates["USD"] = 1.0;
    return out.pivotRates.size() >= kMinCurrencies;
}

} // namespace

bool ParseOpenErApi(const std::string& body, Result& out) {
    try {
        json j = json::parse(body);
        if (!j.is_object() || j.value("result", std::string()) != "success" ||
            j.value("base_code", std::string()) != "USD") {
            out.error = "unexpected response from open.er-api.com";
            return false;
        }
        if (!ReadRates(j["rates"], out)) {
            out.error = "open.er-api.com returned too few valid rates";
            return false;
        }
        auto t = j.find("time_last_update_unix");
        if (t != j.end() && t->is_number_integer() && t->get<long long>() > 0) {
            out.asOf = Utils::FromDayNumber(t->get<long long>() / 86400).ToString();
        }
        out.source = "open.er-api.com";
        out.ok = true;
        return true;
    } catch (const std::exception&) {
        out.error = "open.er-api.com returned invalid JSON";
        return false;
    }
}

bool ParseFrankfurter(const std::string& body, Result& out) {
    try {
        json j = json::parse(body);
        if (!j.is_object() || j.value("base", std::string()) != "USD" || !j.contains("rates")) {
            out.error = "unexpected response from frankfurter.dev";
            return false;
        }
        if (!ReadRates(j["rates"], out)) {
            out.error = "frankfurter.dev returned too few valid rates";
            return false;
        }
        Date d;
        if (Date::TryParse(j.value("date", std::string()), d)) out.asOf = d.ToString();
        out.source = "frankfurter.dev (ECB)";
        out.ok = true;
        return true;
    } catch (const std::exception&) {
        out.error = "frankfurter.dev returned invalid JSON";
        return false;
    }
}

#ifdef _WIN32
namespace {
struct Handle {
    HINTERNET h = nullptr;
    explicit Handle(HINTERNET v) : h(v) {}
    ~Handle() { if (h) WinHttpCloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
} // namespace

bool HttpGet(const std::wstring& host, const std::wstring& path, std::string& body, std::string& error) {
    Handle session(WinHttpOpen(L"ExpenseTrackerPlusPlus/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.h) { error = "could not start the network client"; return false; }
    WinHttpSetTimeouts(session.h, 4000, 4000, 5000, 5000);

    Handle connect(WinHttpConnect(session.h, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connect.h) { error = "could not reach the rate server"; return false; }

    Handle request(WinHttpOpenRequest(connect.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request.h) { error = "could not create the request"; return false; }

    if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        error = "no internet connection or the server did not respond";
        return false;
    }

    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) ||
        status != 200) {
        error = "the rate server answered with HTTP " + std::to_string(status);
        return false;
    }

    body.clear();
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.h, &available)) { error = "the download was interrupted"; return false; }
        if (available == 0) break;
        if (body.size() + available > kMaxBodyBytes) { error = "the response was unexpectedly large"; return false; }
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request.h, chunk.data(), available, &read)) { error = "the download was interrupted"; return false; }
        body.append(chunk, 0, read);
    }
    return true;
}
#else
bool HttpGet(const std::wstring&, const std::wstring&, std::string&, std::string& error) {
    error = "online sync is only available on Windows";
    return false;
}
#endif

Result FetchLatest() {
    Result result;
    try {
        std::string body, error;
        if (HttpGet(L"open.er-api.com", L"/v6/latest/USD", body, error) && ParseOpenErApi(body, result)) return result;
        std::string primaryError = result.error.empty() ? error : result.error;

        result = Result();
        body.clear();
        error.clear();
        if (HttpGet(L"api.frankfurter.dev", L"/v1/latest?base=USD", body, error) && ParseFrankfurter(body, result)) {
            return result;
        }
        result.ok = false;
        result.error = primaryError.empty() ? (result.error.empty() ? error : result.error) : primaryError;
    } catch (const std::exception& e) {
        result.ok = false;
        result.error = e.what();
    }
    return result;
}

} // namespace RateSync
