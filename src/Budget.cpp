#include "Budget.h"
#include "Utils.h"

void BudgetManager::SetOverallBudget(int year, int month, Money limit) {
    int key = MakeKey(year, month);
    if (limit <= 0) overallBudgets.erase(key);
    else overallBudgets[key] = limit;
}

Money BudgetManager::GetOverallBudget(int year, int month) const {
    auto it = overallBudgets.find(MakeKey(year, month));
    return it != overallBudgets.end() ? it->second : 0;
}

bool BudgetManager::HasOverallBudget(int year, int month) const {
    return GetOverallBudget(year, month) > 0;
}

void BudgetManager::SetCategoryBudget(int year, int month, const std::string& category, Money limit) {
    int key = MakeKey(year, month);
    std::string lower = Utils::ToLower(category);
    if (limit <= 0) {
        auto it = categoryBudgets.find(key);
        if (it != categoryBudgets.end()) {
            it->second.erase(lower);
            if (it->second.empty()) categoryBudgets.erase(it);
        }
        return;
    }
    categoryBudgets[key][lower] = CategoryEntry{category, limit};
}

Money BudgetManager::GetCategoryBudget(int year, int month, const std::string& category) const {
    auto it = categoryBudgets.find(MakeKey(year, month));
    if (it == categoryBudgets.end()) return 0;
    auto jt = it->second.find(Utils::ToLower(category));
    return jt != it->second.end() ? jt->second.limit : 0;
}

bool BudgetManager::HasCategoryBudget(int year, int month, const std::string& category) const {
    return GetCategoryBudget(year, month, category) > 0;
}

std::map<std::string, Money> BudgetManager::GetCategoryBudgetsForMonth(int year, int month) const {
    std::map<std::string, Money> result;
    auto it = categoryBudgets.find(MakeKey(year, month));
    if (it == categoryBudgets.end()) return result;
    for (auto& [lower, entry] : it->second) result[entry.displayName] = entry.limit;
    return result;
}

Money BudgetManager::GetCategoryBudgetSum(int year, int month) const {
    Money sum = 0;
    for (auto& [name, limit] : GetCategoryBudgetsForMonth(year, month)) sum += limit;
    return sum;
}

void BudgetManager::ScaleAll(double factor) {
    for (auto& [key, limit] : overallBudgets) limit = MoneyUtil::Scale(limit, factor);
    for (auto& [key, month] : categoryBudgets) {
        for (auto& [lower, entry] : month) entry.limit = MoneyUtil::Scale(entry.limit, factor);
    }
}

void BudgetManager::Clear() {
    overallBudgets.clear();
    categoryBudgets.clear();
}
