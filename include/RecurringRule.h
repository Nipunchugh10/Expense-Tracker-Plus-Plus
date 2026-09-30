#pragma once
#include <set>
#include <string>
#include <vector>
#include "Expense.h"

// Monthly/Yearly are calendar-exact (same date every month/year); EveryNDays
// renews after a fixed number of days (e.g. 24, 28, 84-day plans).
enum class Frequency { Daily, Weekly, Monthly, Yearly, EveryNDays };

class RecurringRule {
public:
    RecurringRule() = default;
    RecurringRule(int id, const std::string& description, Money amount,
                  const std::string& category, const std::string& currency,
                  Frequency freq, const Date& start, const Date& end, bool active = true);

    int                GetID()          const { return id; }
    const std::string& GetDescription() const { return description; }
    Money              GetAmount()      const { return amount; }
    const std::string& GetCategory()    const { return category; }
    const std::string& GetCurrency()    const { return currency; }
    Frequency          GetFrequency()   const { return frequency; }
    const Date&        GetStartDate()   const { return startDate; }
    const Date&        GetEndDate()     const { return endDate; }
    bool               IsActive()       const { return active; }
    const std::string& GetNote()        const { return note; }
    int                GetIntervalDays() const { return intervalDays; }   // used by EveryNDays
    int                GetRenewalMinutes() const { return renewalMinutes; } // time of day, 0..1439
    bool               IsAutoRecord()   const { return autoRecord; }
    std::string        TimeString()     const;                            // "HH:MM"
    std::string        CycleLabel()     const;                            // "Monthly", "Every 28 days", ...

    void SetID(int v)                         { id = v; }
    void SetDescription(const std::string& d) { description = d; }
    void SetAmount(Money a)                   { amount = a; }
    void SetCategory(const std::string& c)    { category = c; }
    void SetCurrency(const std::string& c)    { currency = c; }
    void SetFrequency(Frequency f)            { frequency = f; }
    void SetStartDate(const Date& d)          { startDate = d; }
    void SetEndDate(const Date& d)            { endDate = d; }
    void SetActive(bool a)                    { active = a; }
    void SetNote(const std::string& n)        { note = n; }
    void SetIntervalDays(int d)               { intervalDays = d; }
    void SetRenewalMinutes(int m)             { renewalMinutes = m; }
    void SetAutoRecord(bool a)                { autoRecord = a; }

    // Generation bookkeeping (P0-D3): occurrences are generated only after
    // lastGeneratedThrough, and dates in skippedDates are never regenerated.
    bool        HasGenerated() const                  { return hasGenerated; }
    const Date& GetLastGeneratedThrough() const       { return lastGeneratedThrough; }
    void        SetLastGeneratedThrough(const Date& d){ lastGeneratedThrough = d; hasGenerated = true; }
    void        ClearGenerated()                      { hasGenerated = false; }
    const std::set<int>& GetSkippedDates() const      { return skippedDates; }
    void        AddSkippedDate(const Date& d)         { skippedDates.insert(d.ToInt()); }
    void        RemoveSkippedDate(const Date& d)      { skippedDates.erase(d.ToInt()); }
    bool        IsSkipped(const Date& d) const        { return skippedDates.count(d.ToInt()) > 0; }

    // n-th occurrence computed from the start date, so month-end and leap-day
    // schedules never drift (P0-D2).
    Date NthOccurrence(long long n) const;

    // Occurrences in [rangeStart, rangeEnd] (also bounded by start/end date),
    // at most maxCount entries.
    std::vector<Date> GetOccurrences(const Date& rangeStart, const Date& rangeEnd,
                                     size_t maxCount = 100000) const;

    // First scheduled date on or after `from` within the rule's end date.
    bool NextOccurrenceOnOrAfter(const Date& from, Date& out) const;

    // Next renewal strictly after "now" (a renewal today counts only if its
    // time has not passed yet). nowMinutes < 0 means "start of today".
    bool NextRenewal(const Date& today, int nowMinutes, Date& out) const;

    // Active, not ended, and the next renewal is within daysAhead days.
    bool IsDueSoon(const Date& today, int daysAhead = 7, int nowMinutes = -1) const;

    // Last date that is already "due" at this moment (today if the renewal
    // time has passed, otherwise yesterday).
    Date DueThrough(const Date& today, int nowMinutes) const;

    Money GetAnnualCost() const;          // Daily x365, Weekly x52, Monthly x12, Yearly x1, N days x365/N
    Money GetMonthlyEquivalent() const;   // annual cost / 12

    // Normalizes text/currency and checks amount and dates.
    bool Normalize(std::string& error);

    static std::string FrequencyToString(Frequency f);
    static bool        FrequencyFromString(const std::string& s, Frequency& out);

private:
    long long FirstIndexOnOrAfter(const Date& d) const;

    int         id = 0;
    std::string description;
    Money       amount = 0;
    std::string category;
    std::string currency = kDefaultCurrency;
    Frequency   frequency = Frequency::Monthly;
    Date        startDate;
    Date        endDate = {2100, 12, 31};
    bool        active = true;
    std::string note;
    int         intervalDays = 30;
    int         renewalMinutes = 0;       // 00:00 keeps older data behaving as before
    bool        autoRecord = true;

    bool          hasGenerated = false;
    Date          lastGeneratedThrough;
    std::set<int> skippedDates;   // YYYYMMDD
};
