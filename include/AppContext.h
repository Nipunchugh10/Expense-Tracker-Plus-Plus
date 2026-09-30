#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "Expense.h"

class ExpenseTracker;
class CommandManager;
class AutoCategorizer;
class ICommand;

enum class StatusLevel { Info, Success, Warning, Error };

// What tabs may use. Tabs read the tracker freely but never mutate it while
// rendering: every change is queued with Defer()/DeferCommand() and applied
// by App after the frame's widgets are done (P0-E2), so no pointer obtained
// from the tracker during rendering is ever invalidated mid-loop.
struct AppContext {
    const ExpenseTracker* tracker = nullptr;
    AutoCategorizer*      categorizer = nullptr;
    Date                  today;
    bool                  readOnly = false;

    // Queued mutation with direct tracker access (applied end of frame).
    std::function<void(std::function<bool(ExpenseTracker&)>)> defer;   // return false = failed (lastError shown)
    // Queued undoable command built from the tracker at apply time.
    std::function<void(std::function<std::unique_ptr<ICommand>(ExpenseTracker&)>)> deferCommand;
    std::function<void(StatusLevel, const std::string&)> status;
    std::function<void()> requestRecurringGeneration;
    std::function<void()> requestReset;
    std::function<void()> saveCategorizer;

    void Defer(std::function<bool(ExpenseTracker&)> fn) const { if (defer) defer(std::move(fn)); }
    void DeferCommand(std::function<std::unique_ptr<ICommand>(ExpenseTracker&)> fn) const {
        if (deferCommand) deferCommand(std::move(fn));
    }
    void Status(StatusLevel level, const std::string& msg) const { if (status) status(level, msg); }
};
