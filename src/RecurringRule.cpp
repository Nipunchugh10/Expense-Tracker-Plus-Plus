#include "RecurringRule.h"
#include "Utils.h"
#include "Validation.h"
#include <algorithm>

RecurringRule::RecurringRule(int id_, const std::string& description_, Money amount_,
                             const std::string& category_, const std::string& currency_,
                             Frequency freq_, const Date& start_, const Date& end_, bool active_)
    : id(id_), description(description_), amount(amount_), category(category_),
      currency(currency_), frequency(freq_), startDate(start_), endDate(end_), active(active_)
{
}

Date RecurringRule::NthOccurrence(long long n) const {
    switch (frequency) {
        case Frequency::Daily:   return Utils::AddDays(startDate, n);
        case Frequency::Weekly:  return Utils::AddDays(startDate, n * 7);
        case Frequency::Monthly: return Utils::AddMonths(startDate, n);
        case Frequency::Yearly:  return Utils::AddYears(startDate, n);
        case Frequency::EveryNDays: return Utils::AddDays(startDate, n * std::max(1, intervalDays));
    }
    return startDate;
}

long long RecurringRule::FirstIndexOnOrAfter(const Date& d) const {
    if (d <= startDate) return 0;
    long long n = 0;
    switch (frequency) {
        case Frequency::Daily:
            return Utils::DaysBetween(startDate, d);
        case Frequency::Weekly: {
            long long days = Utils::DaysBetween(startDate, d);
            return (days + 6) / 7;
        }
        case Frequency::EveryNDays: {
            long long days = Utils::DaysBetween(startDate, d);
            long long step = std::max(1, intervalDays);
            return (days + step - 1) / step;
        }
        case Frequency::Monthly:
            n = (static_cast<long long>(d.year) - startDate.year) * 12 + (d.month - startDate.month) - 1;
            break;
        case Frequency::Yearly:
            n = static_cast<long long>(d.year) - startDate.year - 1;
            break;
    }
    if (n < 0) n = 0;
    while (NthOccurrence(n) < d) n++;
    return n;
}

std::vector<Date> RecurringRule::GetOccurrences(const Date& rangeStart, const Date& rangeEnd,
                                                size_t maxCount) const {
    std::vector<Date> dates;
    if (!startDate.IsValid() || !endDate.IsValid() || rangeEnd < rangeStart) return dates;

    for (long long n = FirstIndexOnOrAfter(rangeStart); dates.size() < maxCount; n++) {
        Date d = NthOccurrence(n);
        if (d > rangeEnd || d > endDate) break;
        dates.push_back(d);
    }
    return dates;
}

bool RecurringRule::NextOccurrenceOnOrAfter(const Date& from, Date& out) const {
    auto dates = GetOccurrences(from, endDate, 1);
    if (dates.empty()) return false;
    out = dates.front();
    return true;
}

bool RecurringRule::NextRenewal(const Date& today, int nowMinutes, Date& out) const {
    auto dates = GetOccurrences(today, endDate, 2);
    for (auto& d : dates) {
        if (d > today || nowMinutes < 0 || renewalMinutes > nowMinutes) {
            out = d;
            return true;
        }
    }
    return false;
}

bool RecurringRule::IsDueSoon(const Date& today, int daysAhead, int nowMinutes) const {
    if (!active) return false;
    Date next;
    if (!NextRenewal(today, nowMinutes, next)) return false;
    return Utils::DaysBetween(today, next) <= daysAhead;
}

Date RecurringRule::DueThrough(const Date& today, int nowMinutes) const {
    return (nowMinutes >= renewalMinutes) ? today : Utils::AddDays(today, -1);
}

std::string RecurringRule::TimeString() const {
    int h = renewalMinutes / 60, m = renewalMinutes % 60;
    std::string hh = std::to_string(h), mm = std::to_string(m);
    return (hh.size() < 2 ? "0" + hh : hh) + ":" + (mm.size() < 2 ? "0" + mm : mm);
}

std::string RecurringRule::CycleLabel() const {
    switch (frequency) {
        case Frequency::Daily:   return "Every day";
        case Frequency::Weekly:  return "Every 7 days";
        case Frequency::Monthly: return "Monthly";
        case Frequency::Yearly:  return "Yearly";
        case Frequency::EveryNDays:
            return intervalDays == 1 ? "Every day" : "Every " + std::to_string(intervalDays) + " days";
    }
    return "Monthly";
}

Money RecurringRule::GetAnnualCost() const {
    switch (frequency) {
        case Frequency::Daily:   return amount * 365;
        case Frequency::Weekly:  return amount * 52;
        case Frequency::Monthly: return amount * 12;
        case Frequency::Yearly:  return amount;
        case Frequency::EveryNDays: {
            Money step = std::max(1, intervalDays);
            return (amount * 365 + step / 2) / step;
        }
    }
    return 0;
}

Money RecurringRule::GetMonthlyEquivalent() const {
    Money annual = GetAnnualCost();
    return (annual + 6) / 12;   // rounded to the nearest minor unit
}

bool RecurringRule::Normalize(std::string& error) {
    description = Validation::NormalizeText(description, Validation::kMaxDescriptionBytes);
    if (description.empty()) {
        error = "Name is required.";
        return false;
    }
    if (!Validation::ValidateMoney(amount, error)) return false;
    if (amount == 0) {
        error = "Amount must be greater than zero.";
        return false;
    }
    std::string cur;
    if (!Validation::NormalizeCurrency(currency, cur)) {
        error = "Currency must be a 3-letter code such as INR or USD.";
        return false;
    }
    currency = cur;
    category = Validation::NormalizeCategory(category);
    note = Validation::NormalizeText(note, Validation::kMaxNoteBytes);
    if (!startDate.IsValid()) {
        error = "Start date is not a valid date.";
        return false;
    }
    if (!endDate.IsValid()) {
        error = "End date is not a valid date.";
        return false;
    }
    if (endDate < startDate) {
        error = "End date must be on or after the start date.";
        return false;
    }
    if (frequency == Frequency::EveryNDays && (intervalDays < 1 || intervalDays > 3650)) {
        error = "The renewal period must be between 1 and 3650 days.";
        return false;
    }
    if (renewalMinutes < 0 || renewalMinutes > 23 * 60 + 59) {
        error = "The renewal time must be between 00:00 and 23:59.";
        return false;
    }
    return true;
}

std::string RecurringRule::FrequencyToString(Frequency f) {
    switch (f) {
        case Frequency::Daily:   return "Daily";
        case Frequency::Weekly:  return "Weekly";
        case Frequency::Monthly: return "Monthly";
        case Frequency::Yearly:  return "Yearly";
        case Frequency::EveryNDays: return "EveryNDays";
    }
    return "Monthly";
}

bool RecurringRule::FrequencyFromString(const std::string& s, Frequency& out) {
    std::string v = Utils::ToLower(Utils::Trim(s));
    if (v == "daily")   { out = Frequency::Daily;   return true; }
    if (v == "weekly")  { out = Frequency::Weekly;  return true; }
    if (v == "monthly") { out = Frequency::Monthly; return true; }
    if (v == "yearly")  { out = Frequency::Yearly;  return true; }
    if (v == "everyndays") { out = Frequency::EveryNDays; return true; }
    return false;
}
