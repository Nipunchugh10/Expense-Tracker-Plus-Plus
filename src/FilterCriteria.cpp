#include "FilterCriteria.h"
#include "Utils.h"
#include <utility>

bool FilterCriteria::Matches(const Expense& e) const {
    if (!searchText.empty() &&
        !Utils::ContainsCI(e.GetDescription(), searchText) &&
        !Utils::ContainsCI(e.GetCategory(), searchText)) {
        return false;
    }
    if (e.GetDate() < dateFrom || e.GetDate() > dateTo) return false;
    if (!category.empty() && !Utils::EqualsCI(e.GetCategory(), category)) return false;
    if (!currency.empty() && !Utils::EqualsCI(e.GetCurrency(), currency)) return false;
    if (e.GetAmount() < amountMin || e.GetAmount() > amountMax) return false;
    if (type >= 0 && static_cast<int>(e.GetType()) != type) return false;
    return true;
}

bool FilterCriteria::operator==(const FilterCriteria& o) const {
    return searchText == o.searchText && dateFrom == o.dateFrom && dateTo == o.dateTo && category == o.category &&
           currency == o.currency && amountMin == o.amountMin && amountMax == o.amountMax && type == o.type;
}

void FilterCriteria::Reset() {
    *this = FilterCriteria();
}

bool FilterCriteria::Normalize() {
    bool swapped = false;
    if (dateTo < dateFrom) { std::swap(dateFrom, dateTo); swapped = true; }
    if (amountMax < amountMin) { std::swap(amountMin, amountMax); swapped = true; }
    return swapped;
}
