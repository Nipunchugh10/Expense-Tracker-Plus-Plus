#include "Utils.h"
#include <algorithm>

namespace Utils {

// ── Strings ───────────────────────────────────────────────────────

std::string ToLower(const std::string& s) {
    std::string r = s;
    for (char& c : r) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return r;
}

std::string ToUpper(const std::string& s) {
    std::string r = s;
    for (char& c : r) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return r;
}

bool EqualsCI(const std::string& a, const std::string& b) {
    return a.size() == b.size() && ToLower(a) == ToLower(b);
}

bool ContainsCI(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    return ToLower(haystack).find(ToLower(needle)) != std::string::npos;
}

std::string Trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// ── UTF-8 ─────────────────────────────────────────────────────────

// Length of the valid UTF-8 sequence starting at i, or 0 if invalid.
static size_t Utf8SeqLen(const std::string& s, size_t i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) return 1;
    size_t n;
    unsigned cp;
    if ((c & 0xE0) == 0xC0)      { n = 2; cp = c & 0x1Fu; }
    else if ((c & 0xF0) == 0xE0) { n = 3; cp = c & 0x0Fu; }
    else if ((c & 0xF8) == 0xF0) { n = 4; cp = c & 0x07u; }
    else return 0;
    if (i + n > s.size()) return 0;
    for (size_t k = 1; k < n; k++) {
        unsigned char cc = static_cast<unsigned char>(s[i + k]);
        if ((cc & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (cc & 0x3Fu);
    }
    if ((n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000) ||
        cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        return 0;
    }
    return n;
}

static void AppendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

bool IsValidUtf8(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        size_t n = Utf8SeqLen(s, i);
        if (n == 0) return false;
        i += n;
    }
    return true;
}

std::string SanitizeUtf8(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        size_t n = Utf8SeqLen(s, i);
        if (n == 0) {
            AppendUtf8(out, 0xFFFD);
            i++;
        } else {
            out.append(s, i, n);
            i += n;
        }
    }
    return out;
}

std::string TruncateUtf8(const std::string& s, size_t maxBytes) {
    if (s.size() <= maxBytes) return s;
    size_t cut = maxBytes;
    // Step back over continuation bytes so a code point is never split.
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) cut--;
    return s.substr(0, cut);
}

std::string Cp1252ToUtf8(const std::string& s) {
    static const unsigned kHigh[32] = {
        0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
        0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
    };
    std::string out;
    out.reserve(s.size() + s.size() / 4);
    for (char ch : s) {
        unsigned c = static_cast<unsigned char>(ch);
        if (c >= 0x80 && c <= 0x9F) AppendUtf8(out, kHigh[c - 0x80]);
        else AppendUtf8(out, c);
    }
    return out;
}

// ── CSV ───────────────────────────────────────────────────────────

static bool IsFormulaLead(char c) {
    return c == '=' || c == '+' || c == '-' || c == '@' || c == '\t' || c == '\r';
}

std::string EscapeCSVField(const std::string& field, bool protectFormula) {
    std::string f = field;
    // Spreadsheet formula-injection protection: a leading apostrophe makes
    // Excel/Sheets treat the cell as text (quoting alone does not).
    if (protectFormula && !f.empty() && IsFormulaLead(f[0])) f = "'" + f;

    bool needsQuoting = f.find_first_of(",\"\r\n") != std::string::npos ||
                        (!f.empty() && (f.front() == ' ' || f.back() == ' '));
    if (!needsQuoting) return f;

    std::string escaped = "\"";
    for (char c : f) {
        if (c == '"') escaped += "\"\"";
        else escaped += c;
    }
    escaped += '"';
    return escaped;
}

std::string UnprotectCSVField(const std::string& field) {
    if (field.size() >= 2 && field[0] == '\'' && IsFormulaLead(field[1])) return field.substr(1);
    return field;
}

std::vector<std::vector<std::string>> ParseCSV(const std::string& content) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool inQuotes = false;
    bool fieldQuoted = false;

    auto endField = [&]() {
        row.push_back(field);
        field.clear();
        fieldQuoted = false;
    };
    auto endRow = [&]() {
        endField();
        bool blank = row.size() == 1 && row[0].empty();
        if (!blank) rows.push_back(row);
        row.clear();
    };

    const size_t n = content.size();
    for (size_t i = 0; i < n; i++) {
        char c = content[i];
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < n && content[i + 1] == '"') { field += '"'; i++; }
                else inQuotes = false;
            } else {
                field += c;
            }
            continue;
        }
        switch (c) {
            case '"':
                if (field.empty() && !fieldQuoted) { inQuotes = true; fieldQuoted = true; }
                else field += c;
                break;
            case ',':
                endField();
                break;
            case '\r':
                if (i + 1 < n && content[i + 1] == '\n') i++;
                endRow();
                break;
            case '\n':
                endRow();
                break;
            default:
                field += c;
                break;
        }
    }
    if (!field.empty() || fieldQuoted || !row.empty()) endRow();
    return rows;
}

// ── Calendar math ─────────────────────────────────────────────────

bool IsLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int DaysInMonth(int year, int month) {
    static const int days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    if (month == 2 && IsLeapYear(year)) return 29;
    return days[month];
}

// Howard Hinnant's days_from_civil / civil_from_days.
long long ToDayNumber(const Date& d) {
    long long y = d.year;
    const unsigned m = static_cast<unsigned>(d.month);
    const unsigned dd = static_cast<unsigned>(d.day);
    if (m <= 2) y -= 1;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + dd - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}

Date FromDayNumber(long long z) {
    z += 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long y = static_cast<long long>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    Date r;
    r.year  = static_cast<int>(y + (m <= 2 ? 1 : 0));
    r.month = static_cast<int>(m);
    r.day   = static_cast<int>(d);
    return r;
}

long long DaysBetween(const Date& from, const Date& to) {
    return ToDayNumber(to) - ToDayNumber(from);
}

Date AddDays(const Date& d, long long days) {
    return FromDayNumber(ToDayNumber(d) + days);
}

Date AddMonths(const Date& d, long long months) {
    long long total = static_cast<long long>(d.year) * 12 + (d.month - 1) + months;
    Date r;
    r.year  = static_cast<int>(total >= 0 ? total / 12 : (total - 11) / 12);
    r.month = static_cast<int>(total - static_cast<long long>(r.year) * 12) + 1;
    r.day   = std::min(d.day, DaysInMonth(r.year, r.month));
    return r;
}

Date AddYears(const Date& d, long long years) {
    Date r = d;
    r.year = static_cast<int>(d.year + years);
    r.day = std::min(d.day, DaysInMonth(r.year, r.month));
    return r;
}

Date EndOfMonth(int year, int month) {
    Date r;
    r.year = year;
    r.month = month;
    r.day = DaysInMonth(year, month);
    return r;
}

} // namespace Utils
