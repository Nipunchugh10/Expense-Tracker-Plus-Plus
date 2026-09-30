#pragma once
#include <string>
#include "Money.h"

class Expense;

// Shared validators used by every boundary: UI forms, JSON load, CSV import
// and undo/redo (Phase 0 ground rule 3).
namespace Validation {
    constexpr int kMinYear = 1900;
    constexpr int kMaxYear = 2100;

    constexpr size_t kMaxDescriptionBytes = 2000;
    constexpr size_t kMaxCategoryBytes    = 64;
    constexpr size_t kMaxNoteBytes        = 500;
    constexpr size_t kMaxTitleBytes       = 120;
    constexpr size_t kMaxKeywordBytes     = 64;
    constexpr size_t kMinKeywordBytes     = 3;

    constexpr const char* kFallbackCategory = "General";

    int  ClampYear(int year);

    // Finite, non-negative, <= MoneyUtil::kMaxAmount; rounds to 2 decimals.
    bool ValidateAmount(double major, Money& out, std::string& error);
    bool ValidateMoney(Money m, std::string& error);

    // Sanitizes UTF-8, replaces control characters with spaces, trims and
    // truncates on a code-point boundary.
    std::string NormalizeText(const std::string& s, size_t maxBytes);

    // Trimmed, upper-case, exactly three ASCII letters (ISO 4217 shape).
    bool NormalizeCurrency(const std::string& s, std::string& out);

    // Normalized category; empty becomes the fallback category.
    std::string NormalizeCategory(const std::string& s);

    // Normalizes an expense in place (text, currency, category) and checks
    // date and amount. Returns false with a user-facing reason on failure.
    bool NormalizeExpense(Expense& e, std::string& error);
}
