#pragma once
#include <string>
#include <vector>
#include "Expense.h"

namespace Utils {
    // Strings (ASCII case folding; non-ASCII bytes are left untouched)
    std::string ToLower(const std::string& s);
    std::string ToUpper(const std::string& s);
    bool EqualsCI(const std::string& a, const std::string& b);
    bool ContainsCI(const std::string& haystack, const std::string& needle);
    std::string Trim(const std::string& s);

    // UTF-8
    bool IsValidUtf8(const std::string& s);
    std::string SanitizeUtf8(const std::string& s);                  // invalid sequences -> U+FFFD
    std::string TruncateUtf8(const std::string& s, size_t maxBytes); // never splits a code point
    std::string Cp1252ToUtf8(const std::string& s);

    // CSV (RFC 4180)
    std::string EscapeCSVField(const std::string& field, bool protectFormula);
    std::string UnprotectCSVField(const std::string& field);
    std::vector<std::vector<std::string>> ParseCSV(const std::string& content);

    // Calendar math (proleptic Gregorian)
    bool IsLeapYear(int year);
    int  DaysInMonth(int year, int month);
    long long ToDayNumber(const Date& d);   // days since 1970-01-01
    Date FromDayNumber(long long days);
    long long DaysBetween(const Date& from, const Date& to);   // to - from
    Date AddDays(const Date& d, long long days);
    Date AddMonths(const Date& d, long long months);
    Date AddYears(const Date& d, long long years);
    Date EndOfMonth(int year, int month);
}
