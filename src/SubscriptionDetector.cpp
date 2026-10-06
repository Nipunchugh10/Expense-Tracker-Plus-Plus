#include "SubscriptionDetector.h"
#include "Utils.h"
#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>

namespace {

struct Payment {
    Date date;
    int id;
    Money amount;
    std::string category;
    std::string description;
};

// Tolerance in days between a payment and its scheduled date.
int Tolerance(Frequency f, int intervalDays) {
    switch (f) {
        case Frequency::Daily:      return 0;
        case Frequency::Weekly:     return 1;
        case Frequency::Monthly:    return 3;
        case Frequency::Yearly:     return 7;
        case Frequency::EveryNDays: return std::max(1, intervalDays / 10);
    }
    return 0;
}

double PeriodDays(Frequency f, int intervalDays) {
    switch (f) {
        case Frequency::Daily:      return 1.0;
        case Frequency::Weekly:     return 7.0;
        case Frequency::Monthly:    return 30.4375;
        case Frequency::Yearly:     return 365.25;
        case Frequency::EveryNDays: return std::max(1, intervalDays);
    }
    return 30.0;
}

// Distance in days from `d` to the nearest scheduled date of `rule`.
long long DistanceToSchedule(const RecurringRule& rule, const Date& d) {
    double period = PeriodDays(rule.GetFrequency(), rule.GetIntervalDays());
    long long approx = static_cast<long long>(Utils::DaysBetween(rule.GetStartDate(), d) / period + 0.5);
    long long best = -1;
    for (long long n = std::max(0LL, approx - 1); n <= approx + 1; n++) {
        long long dist = std::llabs(Utils::DaysBetween(rule.NthOccurrence(n), d));
        if (best < 0 || dist < best) best = dist;
    }
    return best;
}

// The most frequent day of the month among the payments (ties: the latest payment's day).
int TypicalDay(const std::vector<Payment>& ps) {
    std::map<int, int> count;
    for (auto& p : ps) count[p.date.day]++;
    int day = ps.back().date.day, bestCount = 0;
    for (auto& [d, c] : count) {
        if (c > bestCount || (c == bestCount && d == ps.back().date.day)) {
            day = d;
            bestCount = c;
        }
    }
    return day;
}

} // namespace

RecurringRule DetectedSubscription::ToRule() const {
    RecurringRule r(0, description, amount, category, currency, frequency, startDate, Date{2100, 12, 31}, !looksStopped);
    r.SetIntervalDays(intervalDays);
    r.SetAutoRecord(true);
    r.SetNote("Detected from past payments");
    r.SetLastGeneratedThrough(lastPayment);
    return r;
}

SubscriptionScan DetectSubscriptions(const std::vector<Expense>& expenses,
                                     const std::vector<RecurringRule>& existingRules,
                                     const Date& today) {
    SubscriptionScan scan;

    std::set<std::string> known;
    for (auto& r : existingRules) known.insert(Utils::ToLower(r.GetDescription()) + "|" + r.GetCurrency());

    std::map<std::string, std::vector<Payment>> groups;
    std::vector<std::string> order;   // first-seen order keeps the result stable
    for (auto& e : expenses) {
        if (e.GetType() != TransactionType::Subscription || e.GetRecurringRuleId() != 0) continue;
        std::string key = Utils::ToLower(e.GetDescription()) + "|" + e.GetCurrency();
        auto [it, inserted] = groups.try_emplace(key);
        if (inserted) order.push_back(key);
        it->second.push_back({e.GetDate(), e.GetID(), e.GetAmount(), e.GetCategory(), e.GetDescription()});
    }

    for (auto& key : order) {
        auto& ps = groups[key];
        std::sort(ps.begin(), ps.end(), [](const Payment& a, const Payment& b) {
            return a.date < b.date || (a.date == b.date && a.id < b.id);
        });
        const std::string name = ps.back().description;
        if (known.count(key)) {
            scan.unresolved.push_back(name + ": already a subscription");
            continue;
        }

        std::vector<Date> dates;
        for (auto& p : ps) {
            if (dates.empty() || !(dates.back() == p.date)) dates.push_back(p.date);
        }
        if (dates.size() < 2) {
            scan.unresolved.push_back(name + ": only one payment, so how often it renews is unknown");
            continue;
        }

        std::vector<long long> gaps;
        for (size_t i = 1; i < dates.size(); i++) gaps.push_back(Utils::DaysBetween(dates[i - 1], dates[i]));
        std::vector<long long> sorted = gaps;
        std::sort(sorted.begin(), sorted.end());
        long long median = sorted[sorted.size() / 2];

        DetectedSubscription d;
        d.description = name;
        d.category = ps.back().category;
        d.currency = key.substr(key.rfind('|') + 1);   // already normalized (upper case)
        d.amount = ps.back().amount;
        d.firstPayment = dates.front();
        d.lastPayment = dates.back();
        for (auto& p : ps) d.expenseIds.push_back(p.id);

        // Candidate schedules, most natural first: a calendar cycle when the typical gap looks like
        // one, then a fixed number of days (e.g. 28-day mobile plans look "monthly" but drift).
        std::vector<std::pair<Frequency, int>> candidates;
        if (median >= 6 && median <= 8) candidates.push_back({Frequency::Weekly, 7});
        if (median >= 26 && median <= 35) candidates.push_back({Frequency::Monthly, 30});
        if (median >= 350 && median <= 380) candidates.push_back({Frequency::Yearly, 365});
        if (median >= 1 && median <= 366) candidates.push_back({Frequency::EveryNDays, static_cast<int>(median)});
        if (candidates.empty()) {
            scan.unresolved.push_back(name + ": payments are too far apart to tell how often it renews");
            continue;
        }

        bool matched = false;
        RecurringRule probe;
        for (auto& [freq, interval] : candidates) {
            d.frequency = freq;
            d.intervalDays = freq == Frequency::EveryNDays ? interval : 30;
            // Anchor the schedule so future renewals land on the usual day.
            switch (freq) {
                case Frequency::Monthly: {
                    // A month that has the typical day (so a 31st-of-the-month bill never drifts to
                    // the 28th) and that is on or before the first payment.
                    const int day = TypicalDay(ps);
                    Date month = {d.firstPayment.year, d.firstPayment.month, 1};
                    for (int k = 0; k < 14; k++, month = Utils::AddMonths(month, -1)) {
                        if (Utils::DaysInMonth(month.year, month.month) < day) continue;
                        Date candidate = {month.year, month.month, day};
                        if (!(d.firstPayment < candidate)) {
                            d.startDate = candidate;
                            break;
                        }
                    }
                    break;
                }
                case Frequency::Yearly:
                    d.startDate = Utils::AddYears(d.lastPayment, -(d.lastPayment.year - d.firstPayment.year));
                    if (d.firstPayment < d.startDate) d.startDate = Utils::AddYears(d.startDate, -1);
                    break;
                default: {
                    long long step = freq == Frequency::Weekly ? 7 : d.intervalDays;
                    long long cycles = (Utils::DaysBetween(d.firstPayment, d.lastPayment) + step / 2) / step;
                    d.startDate = Utils::AddDays(d.lastPayment, -cycles * step);
                    if (d.firstPayment < d.startDate) d.startDate = Utils::AddDays(d.startDate, -step);
                    break;
                }
            }
            probe = d.ToRule();
            probe.SetActive(true);
            const long long tol = Tolerance(freq, d.intervalDays);
            bool fits = true;
            for (auto& date : dates) {
                if (DistanceToSchedule(probe, date) > tol) {
                    fits = false;
                    break;
                }
            }
            if (fits) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            scan.unresolved.push_back(name + ": payment dates are irregular");
            continue;
        }

        if (!probe.NextOccurrenceOnOrAfter(Utils::AddDays(d.lastPayment, 1), d.nextRenewal)) continue;
        // A cycle of grace (at least a week) before calling it stopped.
        long long grace = std::max(7LL, static_cast<long long>(PeriodDays(d.frequency, d.intervalDays) / 4));
        d.looksStopped = Utils::AddDays(d.nextRenewal, grace) < today;
        scan.found.push_back(d);
    }
    return scan;
}
