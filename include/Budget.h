#pragma once
#include <map>
#include <string>
#include "Money.h"

// Budgets are expressed in the ledger's base currency. A limit of 0 means
// "no budget" and removes the entry.
class BudgetManager {
public:
    struct CategoryEntry {
        std::string displayName;
        Money       limit = 0;
    };

    // Overall monthly budget
    void  SetOverallBudget(int year, int month, Money limit);
    Money GetOverallBudget(int year, int month) const;
    bool  HasOverallBudget(int year, int month) const;

    // Per-category monthly budget (category lookup is case-insensitive)
    void  SetCategoryBudget(int year, int month, const std::string& category, Money limit);
    Money GetCategoryBudget(int year, int month, const std::string& category) const;
    bool  HasCategoryBudget(int year, int month, const std::string& category) const;
    std::map<std::string, Money> GetCategoryBudgetsForMonth(int year, int month) const;
    Money GetCategoryBudgetSum(int year, int month) const;

    const std::map<int, Money>& GetOverallBudgets() const { return overallBudgets; }
    const std::map<int, std::map<std::string, CategoryEntry>>& GetAllCategoryBudgets() const {
        return categoryBudgets;
    }

    // Multiplies every limit (used when the base currency changes).
    void ScaleAll(double factor);
    void Clear();
    bool Empty() const { return overallBudgets.empty() && categoryBudgets.empty(); }

    static int  MakeKey(int year, int month) { return year * 100 + month; }
    static void SplitKey(int key, int& year, int& month) { year = key / 100; month = key % 100; }

private:
    std::map<int, Money> overallBudgets;                                 // YYYYMM -> limit
    std::map<int, std::map<std::string, CategoryEntry>> categoryBudgets; // YYYYMM -> lower(category) -> entry
};
