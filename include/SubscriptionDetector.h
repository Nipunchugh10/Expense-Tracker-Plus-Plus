#pragma once
#include <string>
#include <vector>
#include "Expense.h"
#include "RecurringRule.h"

// Finds subscriptions hidden in payment history, for example after a CSV import: transactions of
// type Subscription that are not linked to a subscription yet, grouped by name and currency, whose
// dates follow a regular weekly, monthly, yearly or every-N-days pattern (skipped cycles allowed).
struct DetectedSubscription {
    std::string description;
    std::string category;
    std::string currency;
    Money       amount = 0;                    // the most recent payment
    Frequency   frequency = Frequency::Monthly;
    int         intervalDays = 30;             // EveryNDays only
    Date        startDate;                     // schedule anchor (on or before the first payment)
    Date        firstPayment;
    Date        lastPayment;
    Date        nextRenewal;                   // first scheduled date after the last payment
    bool        looksStopped = false;          // no payment for more than a cycle: created paused
    std::vector<int> expenseIds;               // payments that become linked to the subscription

    // The subscription this becomes (ID 0, paused if it looks stopped, already "recorded" up to the
    // last payment so past payments are never generated a second time).
    RecurringRule ToRule() const;
};

struct SubscriptionScan {
    std::vector<DetectedSubscription> found;
    std::vector<std::string> unresolved;       // "Name: why it was not detected"
};

SubscriptionScan DetectSubscriptions(const std::vector<Expense>& expenses,
                                     const std::vector<RecurringRule>& existingRules,
                                     const Date& today);
