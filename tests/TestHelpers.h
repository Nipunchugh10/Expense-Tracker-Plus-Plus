#pragma once
#include <filesystem>
#include <string>
#include "AtomicFile.h"
#include "ExpenseTracker.h"

namespace test {

// Fresh, empty directory under the system temp folder.
std::filesystem::path TempDir(const std::string& name);
std::filesystem::path Fixture(const std::string& name);
std::string ReadText(const std::filesystem::path& p);
void WriteText(const std::filesystem::path& p, const std::string& text);

Expense MakeExpense(const std::string& desc, double amount, const Date& date,
                    const std::string& category = "Food", const std::string& currency = "INR",
                    TransactionType type = TransactionType::Expense);

} // namespace test
