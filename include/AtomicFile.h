#pragma once
#include <filesystem>
#include <string>

// Result of any file operation. I/O functions never throw; they report
// success or a user-facing failure message (Phase 0 ground rule 4).
struct IoResult {
    bool        ok = false;
    std::string message;

    static IoResult Ok(const std::string& msg = "") { return {true, msg}; }
    static IoResult Fail(const std::string& msg)    { return {false, msg}; }
};

struct AtomicWriteOptions {
    bool keepBackup = true;              // copy the previous file to "<name>.bak" first
    bool simulateFailureAfterTemp = false;   // test hook: stop before replacing the target
};

// Writes to "<target>.tmp", flushes it to disk, optionally rotates the old
// file to "<target>.bak", then atomically replaces the target (P0-A5).
IoResult WriteFileAtomic(const std::filesystem::path& target, const std::string& content,
                         const AtomicWriteOptions& options = AtomicWriteOptions());

IoResult ReadWholeFile(const std::filesystem::path& path, std::string& out);
IoResult CopyFileSafe(const std::filesystem::path& from, const std::filesystem::path& to);
