#pragma once
#include <filesystem>
#include <string>
#include "AtomicFile.h"

// UI preferences stored in settings.json (never ledger data).
struct Settings {
    std::string theme = "DarkModern";
    bool        syncRatesOnLaunch = true;   // download exchange rates at startup
    bool        welcomeCompleted = false;   // first-run welcome screen has been answered

    IoResult Load(const std::filesystem::path& filepath);   // missing file = defaults
    IoResult Save(const std::filesystem::path& filepath) const;
};
