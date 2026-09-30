#pragma once
#include <filesystem>
#include <map>
#include <string>
#include "AtomicFile.h"

// Keyword -> category suggestions (Step 7). Matching is case-insensitive,
// on word boundaries, and the longest matching keyword wins, so "uber eats"
// beats "uber" and "gas" never matches "Las Vegas" (G9). Suggestions never
// overwrite a category the user typed. Owned by App, not a singleton (G3).
class AutoCategorizer {
public:
    AutoCategorizer();

    // Returns "" when nothing matches. `matchedKeyword` receives the keyword.
    std::string SuggestCategory(const std::string& description, std::string* matchedKeyword = nullptr) const;

    bool AddRule(const std::string& keyword, const std::string& category, std::string& error);
    bool RemoveRule(const std::string& keyword);
    const std::map<std::string, std::string>& GetRules() const { return rules; }
    void ResetToDefaults();
    // Restore support: Replace makes the rules exactly `incoming` (normalized);
    // Merge adds keywords that are not known yet. Both return the rule count applied.
    int ReplaceRules(const std::map<std::string, std::string>& incoming);
    int MergeRules(const std::map<std::string, std::string>& incoming);

    IoResult Load(const std::filesystem::path& filepath);   // missing file = defaults
    IoResult Save(const std::filesystem::path& filepath) const;

    // A sensible default keyword for "Remember this": the first word of 3+ letters.
    static std::string SuggestKeyword(const std::string& description);

private:
    std::map<std::string, std::string> rules;   // lowercase keyword -> category
};
