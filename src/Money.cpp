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

// The space characters used for digit grouping (U+00A0, U+202F, U+2009) become a plain ' '.
static std::string NormalizeSpaces(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0xC2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xA0) {
            out += ' ';
            i += 1;
        } else if (c == 0xE2 && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0x80 &&
                   (static_cast<unsigned char>(s[i + 2]) == 0xAF || static_cast<unsigned char>(s[i + 2]) == 0x89)) {
            out += ' ';
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

// Digit groups between grouping marks: western "1,234,567" or Indian "12,34,567".
static bool ValidGroups(const std::vector<size_t>& sizes) {
    if (sizes.size() <= 1) return true;
    bool western = sizes[0] >= 1 && sizes[0] <= 3;
    for (size_t i = 1; i < sizes.size() && western; i++) western = sizes[i] == 3;
    if (western) return true;
    bool indian = sizes.back() == 3 && sizes[0] >= 1 && sizes[0] <= 2;
    for (size_t i = 1; i + 1 < sizes.size() && indian; i++) indian = sizes[i] == 2;
    return indian;
}

bool ParseAmount(const std::string& text, DecimalMark mark, Money& out) {
    const std::string s = Utils::Trim(NormalizeSpaces(text));
    if (s.empty()) return false;
    const char decimal = mark == DecimalMark::Dot ? '.' : ',';
    const char group = mark == DecimalMark::Dot ? ',' : '.';

    size_t i = 0;
    bool negative = false;
    if (s[i] == '+' || s[i] == '-') { negative = (s[i] == '-'); i++; }

    unsigned long long whole = 0;
    int wholeDigits = 0;
    std::vector<size_t> groups(1, 0);
    char groupMark = 0;
    for (; i < s.size(); i++) {
        const char c = s[i];
        if (c >= '0' && c <= '9') {
            if (++wholeDigits > 15) return false;
            whole = whole * 10 + static_cast<unsigned>(c - '0');
            groups.back()++;
            continue;
        }
        if (c == group || c == '\'' || c == ' ') {
            if (groups.back() == 0) return false;                 // leading or doubled mark
            if (groupMark != 0 && c != groupMark) return false;   // mixed grouping marks
            groupMark = c;
            groups.push_back(0);
            continue;
        }
        break;
    }
    if (groups.size() > 1 && (groups.back() == 0 || !ValidGroups(groups))) return false;

    int cents = 0, fracDigits = 0;
    bool roundUp = false;
    if (i < s.size() && s[i] == decimal) {
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

bool ParseDecimal(const std::string& text, Money& out) {
    return ParseAmount(text, DecimalMark::Dot, out);
}

DecimalMark DetectDecimalMark(const std::vector<std::string>& samples, DecimalMark fallback) {
    int dot = 0, comma = 0;
    for (const auto& sample : samples) {
        const std::string t = Utils::Trim(NormalizeSpaces(sample));
        const size_t lastDot = t.rfind('.');
        const size_t lastComma = t.rfind(',');
        const bool hasDot = lastDot != std::string::npos;
        const bool hasComma = lastComma != std::string::npos;
        if (!hasDot && !hasComma) continue;
        if (hasDot && hasComma) {   // "1,234.56" / "1.234,56": the last mark is the decimal one
            (lastDot > lastComma ? dot : comma)++;
            continue;
        }
        const char mark = hasDot ? '.' : ',';
        const size_t pos = hasDot ? lastDot : lastComma;
        size_t count = 0;
        for (char c : t) count += (c == mark) ? 1 : 0;
        if (count > 1) {            // "1.234.567": a repeated mark is grouping
            (mark == '.' ? comma : dot)++;
            continue;
        }
        size_t after = 0;
        for (size_t j = pos + 1; j < t.size() && t[j] >= '0' && t[j] <= '9'; j++) after++;
        if (after == 3 || after == 0) continue;   // "1,234" / "1.234" could be either
        (mark == '.' ? dot : comma)++;
    }
    if (dot > comma) return DecimalMark::Dot;
    if (comma > dot) return DecimalMark::Comma;
    return fallback;
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
