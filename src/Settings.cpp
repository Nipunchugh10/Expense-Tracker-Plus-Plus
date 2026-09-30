#include "Settings.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

IoResult Settings::Load(const std::filesystem::path& filepath) {
    std::error_code ec;
    if (!std::filesystem::exists(filepath, ec)) return IoResult::Ok();
    std::string text;
    IoResult r = ReadWholeFile(filepath, text);
    if (!r.ok) return r;
    try {
        json j = json::parse(text);
        if (j.is_object() && j.contains("theme") && j["theme"].is_string()) theme = j["theme"].get<std::string>();
        if (j.is_object() && j.contains("syncRatesOnLaunch") && j["syncRatesOnLaunch"].is_boolean()) {
            syncRatesOnLaunch = j["syncRatesOnLaunch"].get<bool>();
        }
        if (j.is_object() && j.contains("welcomeCompleted") && j["welcomeCompleted"].is_boolean()) {
            welcomeCompleted = j["welcomeCompleted"].get<bool>();
        }
        return IoResult::Ok();
    } catch (const std::exception&) {
        return IoResult::Fail("settings.json is not valid JSON; using default settings.");
    }
}

IoResult Settings::Save(const std::filesystem::path& filepath) const {
    try {
        json j;
        j["version"] = 1;
        j["theme"] = theme;
        j["syncRatesOnLaunch"] = syncRatesOnLaunch;
        j["welcomeCompleted"] = welcomeCompleted;
        AtomicWriteOptions opts;
        opts.keepBackup = false;
        return WriteFileAtomic(filepath, j.dump(2, ' ', false, json::error_handler_t::replace), opts);
    } catch (const std::exception& e) {
        return IoResult::Fail(std::string("Saving settings failed: ") + e.what());
    }
}
