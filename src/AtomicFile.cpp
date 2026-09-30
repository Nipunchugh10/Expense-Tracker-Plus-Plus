#include "AtomicFile.h"
#include "Paths.h"
#include <fstream>
#include <iterator>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

static std::string Display(const fs::path& p) {
    return Paths::ToUtf8(p);
}

#ifdef _WIN32
static std::string LastErrorText() {
    DWORD code = GetLastError();
    wchar_t* buf = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, code, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::string text = "error " + std::to_string(code);
    if (n > 0 && buf) {
        std::wstring w(buf, n);
        while (!w.empty() && (w.back() == L'\n' || w.back() == L'\r' || w.back() == L'.')) w.pop_back();
        text = Paths::ToUtf8(fs::path(w)) + " (" + text + ")";
    }
    if (buf) LocalFree(buf);
    return text;
}
#endif

IoResult WriteFileAtomic(const fs::path& target, const std::string& content, const AtomicWriteOptions& options) {
    std::error_code ec;
    if (target.has_parent_path()) {
        fs::create_directories(target.parent_path(), ec);
        if (ec) return IoResult::Fail("Cannot create folder " + Display(target.parent_path()) + ": " + ec.message());
    }

    fs::path tmp = target;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) return IoResult::Fail("Cannot open " + Display(tmp) + " for writing.");
        f.write(content.data(), static_cast<std::streamsize>(content.size()));
        f.flush();
        if (!f.good()) {
            f.close();
            fs::remove(tmp, ec);
            return IoResult::Fail("Writing " + Display(target) + " failed (disk full or no permission?).");
        }
    }

#ifdef _WIN32
    // Force the temp file's contents to disk before it replaces the original.
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(h);
        CloseHandle(h);
    }
#endif

    if (options.simulateFailureAfterTemp) return IoResult::Fail("Simulated failure after writing the temp file.");

    bool exists = fs::exists(target, ec);
    if (exists && options.keepBackup) {
        fs::path bak = target;
        bak += ".bak";
        fs::copy_file(target, bak, fs::copy_options::overwrite_existing, ec);
        // A failed backup copy is not fatal: the atomic replace still protects the data.
    }

#ifdef _WIN32
    if (!MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::string err = LastErrorText();
        fs::remove(tmp, ec);
        return IoResult::Fail("Could not replace " + Display(target) + ": " + err);
    }
#else
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return IoResult::Fail("Could not replace " + Display(target) + ": " + ec.message());
    }
#endif
    return IoResult::Ok();
}

IoResult ReadWholeFile(const fs::path& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return IoResult::Fail("Cannot open " + Display(path) + ".");
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (f.bad()) return IoResult::Fail("Reading " + Display(path) + " failed.");
    out = std::move(content);
    return IoResult::Ok();
}

IoResult CopyFileSafe(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    if (to.has_parent_path()) fs::create_directories(to.parent_path(), ec);
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) return IoResult::Fail("Could not copy " + Display(from) + " to " + Display(to) + ": " + ec.message());
    return IoResult::Ok();
}
