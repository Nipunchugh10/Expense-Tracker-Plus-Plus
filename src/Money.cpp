#include "Money.h"
#include "Utils.h"
#include <cmath>

namespace MoneyUtil {

Money FromMajor(double major) {
    return static_cast<Money>(std::llround(major * 100.0));
}

double ToMajor(Money m) {
    return static_cast<double>(m) / 100.0;
}

static unsigned long long AbsU(Money m) {
    return m < 0 ? 0ULL - static_cast<unsigned long long>(m) : static_cast<unsigned long long>(m);
}

static std::string TwoDigits(unsigned long long v) {
    std::string s = std::to_string(v);
    return s.size() < 2 ? "0" + s : s;
}

std::string ToDecimalString(Money m) {
    unsigned long long a = AbsU(m);
    return (m < 0 ? "-" : "") + std::to_string(a / 100) + "." + TwoDigits(a % 100);
}

bool ParseDecimal(const std::string& text, Money& out) {
    std::string s;
    for (char c : Utils::Trim(text)) {
        if (c == ',' || c == ' ' || c == '_') continue;
        s += c;
    }
    if (s.empty()) return false;

    size_t i = 0;
    bool negative = false;
    if (s[i] == '+' || s[i] == '-') { negative = (s[i] == '-'); i++; }

    unsigned long long whole = 0;
    int wholeDigits = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        if (++wholeDigits > 15) return false;
        whole = whole * 10 + static_cast<unsigned>(s[i] - '0');
        i++;
    }

    int cents = 0, fracDigits = 0;
    bool roundUp = false;
    if (i < s.size() && s[i] == '.') {
        i++;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            int d = s[i] - '0';
            if (fracDigits < 2) cents = cents * 10 + d;
            else if (fracDigits == 2) roundUp = (d >= 5);
            fracDigits++;
            i++;
        }
        if (fracDigits == 1) cents *= 10;
    }
    if (i != s.size()) return false;
    if (wholeDigits == 0 && fracDigits == 0) return false;

    unsigned long long total = whole * 100 + static_cast<unsigned long long>(cents) + (roundUp ? 1 : 0);
    out = negative ? -static_cast<Money>(total) : static_cast<Money>(total);
    return true;
}

std::string CurrencySymbol(const std::string& code) {
    if (code == "USD") return "$";
    if (code == "EUR") return "\xE2\x82\xAC";   // €
    if (code == "GBP") return "\xC2\xA3";       // £
    if (code == "INR") return "\xE2\x82\xB9";   // ₹
    if (code == "JPY") return "\xC2\xA5";       // ¥
    if (code == "CAD") return "C$";
    if (code == "AUD") return "A$";
    return "";
}

static std::string GroupDigits(unsigned long long whole, bool indian) {
    std::string d = std::to_string(whole);
    if (d.size() <= 3) return d;
    std::string out = d.substr(d.size() - 3);
    size_t pos = d.size() - 3;
    const size_t group = indian ? 2 : 3;
    while (pos > 0) {
        size_t take = pos >= group ? group : pos;
        out = d.substr(pos - take, take) + "," + out;
        pos -= take;
    }
    return out;
}

std::string Format(Money m, const std::string& currency) {
    unsigned long long a = AbsU(m);
    std::string sym = CurrencySymbol(currency);
    std::string prefix = sym.empty() ? (currency.empty() ? "" : currency + " ") : sym;
    return (m < 0 ? "-" : "") + prefix + GroupDigits(a / 100, currency == "INR") + "." + TwoDigits(a % 100);
}

Money Scale(Money m, double factor) {
    return static_cast<Money>(std::llround(static_cast<double>(m) * factor));
}

} // namespace MoneyUtil
