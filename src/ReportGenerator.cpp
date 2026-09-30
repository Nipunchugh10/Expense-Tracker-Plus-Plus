#include "ReportGenerator.h"
#include "ExpenseTracker.h"
#include "Utils.h"
#include <cstdio>
#include <map>
#include <vector>

namespace ReportGenerator {

namespace {

const char* const kMonthNames[12] = {"January", "February", "March", "April", "May", "June",
                                     "July", "August", "September", "October", "November", "December"};

struct BudgetRow {
    std::string category;
    Money allocated = 0;
    Money actual = 0;
    std::string status;
};

struct Row {
    std::vector<std::string> cells;
};

struct Table {
    std::vector<std::string> headers;
    std::vector<Row> rows;
};

std::string Percent(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f%%", v);
    return buf;
}

std::string StatusFor(Money actual, Money allocated) {
    if (allocated <= 0) return "No budget";
    if (actual > allocated) return "Over budget";
    if (actual == allocated) return "At limit";
    if (actual * 100 >= allocated * 75) return "Warning";
    return "Safe";
}

struct ReportData {
    std::string title;
    std::string subtitle;
    std::vector<std::pair<std::string, std::string>> summary;
    std::string note;
    Table budgets;
    Table top;
    Table subscriptions;
};

ReportData Build(const ExpenseTracker& t, int year, int month, const Date& today) {
    ReportData d;
    const std::string base = t.GetBaseCurrency();
    auto fmt = [&](Money m) { return MoneyUtil::Format(m, base); };

    d.title = std::string("Financial Report - ") + kMonthNames[month - 1] + " " + std::to_string(year);
    d.subtitle = "Generated " + today.ToString() + " | Amounts in " + base;

    Money income = t.GetMonthlyIncome(year, month);
    Money expenses = t.GetMonthlyTotal(year, month);
    auto rate = t.GetSavingsRate(year, month);
    d.summary = {
        {"Total Income", fmt(income)},
        {"Total Expenses", fmt(expenses)},
        {"Net Savings", fmt(income - expenses)},
        {"Savings Rate", rate ? Percent(*rate) : std::string("N/A (no income)")},
    };
    int excluded = t.CountUnconvertible(year, month);
    if (excluded > 0) {
        d.note = std::to_string(excluded) + " transaction(s) use a currency without an exchange rate and are "
                 "excluded from these totals.";
    }

    // Budget adherence
    d.budgets.headers = {"Category", "Allocated", "Actual", "Variance", "Status"};
    auto spending = t.GetCategoryBreakdown(year, month);
    auto budgets = t.GetBudgetManager().GetCategoryBudgetsForMonth(year, month);
    std::map<std::string, BudgetRow> rows;
    for (auto& [cat, limit] : budgets) rows[Utils::ToLower(cat)] = {cat, limit, 0, ""};
    for (auto& [cat, spent] : spending) {
        auto& r = rows[Utils::ToLower(cat)];
        if (r.category.empty()) r.category = cat;
        r.actual = spent;
    }
    for (auto& [key, r] : rows) {
        d.budgets.rows.push_back({{r.category, r.allocated > 0 ? fmt(r.allocated) : "-", fmt(r.actual),
                                   r.allocated > 0 ? fmt(r.allocated - r.actual) : "-",
                                   StatusFor(r.actual, r.allocated)}});
    }
    Money overall = t.GetBudgetManager().GetOverallBudget(year, month);
    if (overall > 0) {
        d.budgets.rows.push_back({{"Overall monthly budget", fmt(overall), fmt(expenses),
                                   fmt(overall - expenses), StatusFor(expenses, overall)}});
    }

    // Top 10
    d.top.headers = {"#", "Date", "Description", "Category", "Amount"};
    int rank = 1;
    for (auto* e : t.GetTopExpenses(year, 10, month)) {
        Money converted = 0;
        t.ToBase(*e, converted);
        std::string amount = fmt(converted);
        if (e->GetCurrency() != base) amount += " (" + MoneyUtil::Format(e->GetAmount(), e->GetCurrency()) + ")";
        d.top.rows.push_back({{std::to_string(rank++), e->GetDate().ToString(), e->GetDescription(),
                               e->GetCategory(), amount}});
    }

    // Subscriptions
    d.subscriptions.headers = {"Service", "Cycle", "Amount", "Monthly equivalent", "Next due"};
    for (auto& r : t.GetRecurringRules()) {
        if (!r.IsActive()) continue;
        Money monthly = 0;
        std::string monthlyText = t.GetCurrency().ConvertToBase(r.GetMonthlyEquivalent(), r.GetCurrency(), monthly)
                                      ? fmt(monthly) : "no rate for " + r.GetCurrency();
        Date next;
        std::string nextText = r.NextOccurrenceOnOrAfter(today, next) ? next.ToString() + " " + r.TimeString() : "ended";
        d.subscriptions.rows.push_back({{r.GetDescription(), r.CycleLabel(),
                                         MoneyUtil::Format(r.GetAmount(), r.GetCurrency()), monthlyText, nextText}});
    }
    return d;
}

std::string HtmlTable(const Table& t, const char* empty) {
    if (t.rows.empty()) return std::string("<p class=\"muted\">") + empty + "</p>\n";
    std::string s = "<table>\n<thead><tr>";
    for (auto& h : t.headers) s += "<th>" + EscapeHtml(h) + "</th>";
    s += "</tr></thead>\n<tbody>\n";
    for (auto& r : t.rows) {
        s += "<tr>";
        for (auto& c : r.cells) s += "<td>" + EscapeHtml(c) + "</td>";
        s += "</tr>\n";
    }
    return s + "</tbody>\n</table>\n";
}

std::string MdTable(const Table& t, const char* empty) {
    if (t.rows.empty()) return std::string("_") + empty + "_\n";
    std::string s = "|";
    for (auto& h : t.headers) s += " " + EscapeMarkdown(h) + " |";
    s += "\n|";
    for (size_t i = 0; i < t.headers.size(); i++) s += " --- |";
    s += "\n";
    for (auto& r : t.rows) {
        s += "|";
        for (auto& c : r.cells) s += " " + EscapeMarkdown(c) + " |";
        s += "\n";
    }
    return s;
}

} // namespace

std::string EscapeHtml(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;"; break;
            case '<':  out += "&lt;"; break;
            case '>':  out += "&gt;"; break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default:   out += c; break;
        }
    }
    return out;
}

std::string EscapeMarkdown(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '\\': case '|': case '*': case '_': case '`': case '[': case ']': case '<': case '>': case '#':
                out += '\\';
                out += c;
                break;
            case '\n': case '\r':
                out += ' ';
                break;
            default:
                out += c;
                break;
        }
    }
    return out;
}

std::string Generate(const ExpenseTracker& tracker, int year, int month, Format format, const Date& today) {
    if (month < 1 || month > 12) month = 1;
    ReportData d = Build(tracker, year, month, today);

    if (format == Format::Markdown) {
        std::string s = "# " + EscapeMarkdown(d.title) + "\n\n_" + EscapeMarkdown(d.subtitle) + "_\n\n";
        s += "## Executive Summary\n\n| Metric | Value |\n| --- | --- |\n";
        for (auto& [k, v] : d.summary) s += "| " + EscapeMarkdown(k) + " | " + EscapeMarkdown(v) + " |\n";
        if (!d.note.empty()) s += "\n> " + EscapeMarkdown(d.note) + "\n";
        s += "\n## Budget Adherence\n\n" + MdTable(d.budgets, "No budgets or spending this month.");
        s += "\n## Top 10 Largest Transactions\n\n" + MdTable(d.top, "No expenses this month.");
        s += "\n## Active Recurring Subscriptions\n\n" + MdTable(d.subscriptions, "No active subscriptions.");
        return s;
    }

    std::string s =
        "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
        "<title>" + EscapeHtml(d.title) + "</title>\n<style>\n"
        "body{font-family:'Segoe UI',system-ui,-apple-system,sans-serif;color:#1f2328;background:#fff;"
        "max-width:960px;margin:32px auto;padding:0 20px;line-height:1.5}\n"
        "h1{font-size:26px;margin:0 0 4px}h2{font-size:18px;margin:32px 0 10px;border-bottom:1px solid #d0d7de;"
        "padding-bottom:4px}\n.muted{color:#656d76}\n"
        ".kpis{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px;margin-top:20px}\n"
        ".kpi{border:1px solid #d0d7de;border-radius:8px;padding:12px 14px}\n"
        ".kpi .label{font-size:12px;color:#656d76;text-transform:uppercase;letter-spacing:.04em}\n"
        ".kpi .value{font-size:20px;font-weight:600;font-variant-numeric:tabular-nums}\n"
        "table{width:100%;border-collapse:collapse;font-size:14px}\n"
        "th,td{text-align:left;padding:6px 8px;border-bottom:1px solid #eaeef2}\n"
        "th{background:#f6f8fa;font-weight:600}td{font-variant-numeric:tabular-nums}\n"
        ".note{background:#fff8c5;border:1px solid #d4a72c;border-radius:6px;padding:8px 12px;margin-top:16px}\n"
        "@media print{body{margin:0}.kpi{break-inside:avoid}}\n"
        "</style>\n</head>\n<body>\n";
    s += "<h1>" + EscapeHtml(d.title) + "</h1>\n<div class=\"muted\">" + EscapeHtml(d.subtitle) + "</div>\n";
    s += "<h2>Executive Summary</h2>\n<div class=\"kpis\">\n";
    for (auto& [k, v] : d.summary) {
        s += "<div class=\"kpi\"><div class=\"label\">" + EscapeHtml(k) + "</div><div class=\"value\">" +
             EscapeHtml(v) + "</div></div>\n";
    }
    s += "</div>\n";
    if (!d.note.empty()) s += "<div class=\"note\">" + EscapeHtml(d.note) + "</div>\n";
    s += "<h2>Budget Adherence</h2>\n" + HtmlTable(d.budgets, "No budgets or spending this month.");
    s += "<h2>Top 10 Largest Transactions</h2>\n" + HtmlTable(d.top, "No expenses this month.");
    s += "<h2>Active Recurring Subscriptions</h2>\n" + HtmlTable(d.subscriptions, "No active subscriptions.");
    s += "</body>\n</html>\n";
    return s;
}

} // namespace ReportGenerator
