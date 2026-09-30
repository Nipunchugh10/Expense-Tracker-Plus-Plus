#include "TestFramework.h"
#include "TestHelpers.h"
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

namespace test {

fs::path TempDir(const std::string& name) {
    std::error_code ec;
    fs::path dir = fs::temp_directory_path(ec) / "expense_tracker_tests" / name;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

fs::path Fixture(const std::string& name) {
    return fs::path(FIXTURE_DIR) / name;
}

std::string ReadText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void WriteText(const fs::path& p, const std::string& text) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
}

Expense MakeExpense(const std::string& desc, double amount, const Date& date, const std::string& category,
                    const std::string& currency, TransactionType type) {
    return Expense(0, desc, MoneyUtil::FromMajor(amount), date, category, currency, 0, type);
}

} // namespace test

int main() {
    int failedCases = 0;
    for (auto& c : tf::Registry()) {
        int before = tf::Failures();
        try {
            c.fn();
        } catch (const tf::Abort&) {
        } catch (const std::exception& e) {
            tf::Failures()++;
            std::cerr << "Unhandled exception in '" << c.name << "': " << e.what() << "\n";
        }
        bool ok = tf::Failures() == before;
        if (!ok) failedCases++;
        std::cout << (ok ? "[ PASS ] " : "[ FAIL ] ") << c.name << "\n";
    }
    std::cout << "\n" << tf::Registry().size() - static_cast<size_t>(failedCases) << "/" << tf::Registry().size()
              << " test cases passed, " << tf::Failures() << " failed checks.\n";
    return failedCases == 0 ? 0 : 1;
}
