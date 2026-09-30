#pragma once
#include <string>
#include "Expense.h"

class ExpenseTracker;

// Monthly financial report (Step 10). All user text is escaped for the
// target format (G12).
namespace ReportGenerator {
    enum class Format { Html, Markdown };

    std::string Generate(const ExpenseTracker& tracker, int year, int month, Format format, const Date& today);

    std::string EscapeHtml(const std::string& s);
    std::string EscapeMarkdown(const std::string& s);
}
