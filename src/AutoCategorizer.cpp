#include "AutoCategorizer.h"
#include "Utils.h"
#include "Validation.h"
#include <nlohmann/json.hpp>
#include <vector>

using json = nlohmann::json;

namespace {

bool IsWordChar(char c) {
    unsigned char u = static_cast<unsigned char>(c);
    return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u >= 0x80;
}

// True when `keyword` occurs in `text` delimited by non-word characters.
bool ContainsWord(const std::string& text, const std::string& keyword) {
    for (size_t pos = text.find(keyword); pos != std::string::npos; pos = text.find(keyword, pos + 1)) {
        size_t end = pos + keyword.size();
        bool startOk = pos == 0 || !IsWordChar(text[pos - 1]);
        bool endOk = end == text.size() || !IsWordChar(text[end]);
        if (startOk && endOk) return true;
    }
    return false;
}

std::string NormalizeKeyword(const std::string& keyword) {
    std::string k = Utils::ToLower(Validation::NormalizeText(keyword, Validation::kMaxKeywordBytes));
    std::string collapsed;
    for (char c : k) {
        if (c == ' ' && !collapsed.empty() && collapsed.back() == ' ') continue;
        collapsed += c;
    }
    return collapsed;
}

} // namespace

AutoCategorizer::AutoCategorizer() {
    ResetToDefaults();
}

void AutoCategorizer::ResetToDefaults() {
    struct Group { const char* category; std::vector<const char*> keywords; };
    static const std::vector<Group> defaults = {
        {"Groceries", {"walmart", "costco", "aldi", "trader joe", "supermarket", "kroger", "safeway",
                       "grocery", "bigbasket", "blinkit", "zepto", "dmart", "jiomart", "reliance fresh"}},
        {"Dining", {"starbucks", "mcdonald", "subway", "chipotle", "uber eats", "doordash", "cafe", "pizza",
                    "swiggy", "zomato", "domino", "kfc", "restaurant"}},
        {"Transportation", {"uber", "lyft", "shell", "chevron", "petrol", "fuel", "gas station", "metro", "train",
                            "ola", "rapido", "irctc", "indian oil", "bharat petroleum", "hpcl", "bpcl",
                            "parking", "toll"}},
        {"Entertainment", {"netflix", "spotify", "steam", "playstation", "cinema", "amc", "hulu",
                           "prime video", "amazon prime", "hotstar", "bookmyshow", "pvr"}},
        {"Shopping", {"amazon", "flipkart", "myntra", "ajio", "meesho", "ikea", "decathlon", "croma",
                      "reliance digital", "target", "best buy"}},
        {"Health", {"pharmacy", "apollo", "medplus", "pharmeasy", "1mg", "hospital", "clinic", "doctor",
                    "dental", "practo"}},
        {"Utilities", {"electric", "electricity", "water bill", "internet", "at&t", "verizon", "comcast",
                       "airtel", "jio", "broadband", "gas bill"}},
    };
    rules.clear();
    for (auto& g : defaults) {
        for (auto* k : g.keywords) rules[k] = g.category;
    }
}

std::string AutoCategorizer::SuggestCategory(const std::string& description, std::string* matchedKeyword) const {
    std::string text = Utils::ToLower(description);
    const std::string* best = nullptr;
    const std::string* bestKeyword = nullptr;
    for (auto& [keyword, category] : rules) {
        if (bestKeyword && keyword.size() <= bestKeyword->size()) continue;   // longest keyword wins
        if (ContainsWord(text, keyword)) {
            best = &category;
            bestKeyword = &keyword;
        }
    }
    if (!best) return "";
    if (matchedKeyword) *matchedKeyword = *bestKeyword;
    return *best;
}

bool AutoCategorizer::AddRule(const std::string& keyword, const std::string& category, std::string& error) {
    std::string k = NormalizeKeyword(keyword);
    if (k.size() < Validation::kMinKeywordBytes) {
        error = "Keywords must be at least 3 characters long.";
        return false;
    }
    std::string c = Validation::NormalizeCategory(category);
    rules[k] = c;
    return true;
}

int AutoCategorizer::ReplaceRules(const std::map<std::string, std::string>& incoming) {
    std::map<std::string, std::string> next;
    for (auto& [keyword, category] : incoming) {
        std::string k = NormalizeKeyword(keyword);
        if (k.size() >= Validation::kMinKeywordBytes) next[k] = Validation::NormalizeCategory(category);
    }
    if (next.empty()) return 0;   // never wipe the rules with an empty/invalid set
    rules = std::move(next);
    return static_cast<int>(rules.size());
}

int AutoCategorizer::MergeRules(const std::map<std::string, std::string>& incoming) {
    int added = 0;
    for (auto& [keyword, category] : incoming) {
        std::string k = NormalizeKeyword(keyword);
        if (k.size() < Validation::kMinKeywordBytes || rules.count(k)) continue;
        rules[k] = Validation::NormalizeCategory(category);
        added++;
    }
    return added;
}

bool AutoCategorizer::RemoveRule(const std::string& keyword) {
    return rules.erase(NormalizeKeyword(keyword)) > 0;
}

std::string AutoCategorizer::SuggestKeyword(const std::string& description) {
    std::string text = Utils::ToLower(Validation::NormalizeText(description, Validation::kMaxDescriptionBytes));
    std::string word;
    for (size_t i = 0; i <= text.size(); i++) {
        if (i < text.size() && IsWordChar(text[i])) {
            word += text[i];
            continue;
        }
        if (word.size() >= Validation::kMinKeywordBytes) return word;
        word.clear();
    }
    return "";
}

IoResult AutoCategorizer::Load(const std::filesystem::path& filepath) {
    std::error_code ec;
    if (!std::filesystem::exists(filepath, ec)) {
        ResetToDefaults();
        return IoResult::Ok();
    }
    std::string text;
    IoResult r = ReadWholeFile(filepath, text);
    if (!r.ok) return r;
    try {
        json j = json::parse(text);
        auto it = j.find("rules");
        if (!j.is_object() || it == j.end() || !it->is_object()) {
            return IoResult::Fail("Category rules file has an unexpected format; using defaults.");
        }
        std::map<std::string, std::string> loaded;
        for (auto& [keyword, category] : it->items()) {
            std::string k = NormalizeKeyword(keyword);
            if (k.size() < Validation::kMinKeywordBytes || !category.is_string()) continue;
            loaded[k] = Validation::NormalizeCategory(category.get<std::string>());
        }
        rules = std::move(loaded);
        return IoResult::Ok();
    } catch (const std::exception& e) {
        return IoResult::Fail(std::string("Category rules file is not valid JSON; using defaults (") + e.what() + ").");
    }
}

IoResult AutoCategorizer::Save(const std::filesystem::path& filepath) const {
    try {
        json j;
        j["version"] = 1;
        j["rules"] = rules;
        return WriteFileAtomic(filepath, j.dump(2, ' ', false, json::error_handler_t::replace));
    } catch (const std::exception& e) {
        return IoResult::Fail(std::string("Saving category rules failed: ") + e.what());
    }
}
