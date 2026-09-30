#include "CommandManager.h"
#include "ExpenseTracker.h"

// ── CommandManager ────────────────────────────────────────────────

bool CommandManager::ExecuteCommand(std::unique_ptr<ICommand> cmd, std::string& error) {
    if (!cmd) return false;
    if (!cmd->Execute()) {
        error = cmd->GetError();
        return false;
    }
    undoStack.push_back(std::move(cmd));
    if (undoStack.size() > limit) undoStack.erase(undoStack.begin());
    redoStack.clear();
    return true;
}

bool CommandManager::Undo(std::string& name, std::string& error) {
    if (undoStack.empty()) return false;
    std::unique_ptr<ICommand> cmd = std::move(undoStack.back());
    undoStack.pop_back();
    name = cmd->GetName();
    if (!cmd->Undo()) {
        error = cmd->GetError();
        return false;   // the command no longer applies; drop it
    }
    redoStack.push_back(std::move(cmd));
    return true;
}

bool CommandManager::Redo(std::string& name, std::string& error) {
    if (redoStack.empty()) return false;
    std::unique_ptr<ICommand> cmd = std::move(redoStack.back());
    redoStack.pop_back();
    name = cmd->GetName();
    if (!cmd->Execute()) {
        error = cmd->GetError();
        return false;
    }
    undoStack.push_back(std::move(cmd));
    return true;
}

std::string CommandManager::PeekUndoName() const {
    return undoStack.empty() ? "" : undoStack.back()->GetName();
}

std::string CommandManager::PeekRedoName() const {
    return redoStack.empty() ? "" : redoStack.back()->GetName();
}

void CommandManager::Clear() {
    undoStack.clear();
    redoStack.clear();
}

// ── Commands ──────────────────────────────────────────────────────

static std::string Describe(const char* verb, const Expense& e) {
    return std::string(verb) + " " + TransactionTypeLabel(e.GetType()) + " (#" + std::to_string(e.GetID()) + ")";
}

bool AddExpenseCommand::Execute() {
    if (!executedOnce) {
        int newId = tracker.AddExpense(snapshot);
        if (newId == 0) {
            error = tracker.GetLastError();
            return false;
        }
        snapshot = *tracker.FindExpense(newId);   // normalized copy with its ID
        executedOnce = true;
        return true;
    }
    if (!tracker.InsertExpense(snapshot)) {
        error = tracker.GetLastError();
        return false;
    }
    return true;
}

bool AddExpenseCommand::Undo() {
    if (!tracker.DeleteExpense(snapshot.GetID(), false)) {
        error = tracker.GetLastError();
        return false;
    }
    return true;
}

std::string AddExpenseCommand::GetName() const {
    return Describe("Add", snapshot);
}

bool DeleteExpenseCommand::Execute() {
    const Expense* e = tracker.FindExpense(id);
    if (!e) {
        error = "Expense #" + std::to_string(id) + " no longer exists.";
        return false;
    }
    snapshot = *e;
    if (!tracker.DeleteExpense(id, true)) {
        error = tracker.GetLastError();
        return false;
    }
    return true;
}

bool DeleteExpenseCommand::Undo() {
    if (!tracker.InsertExpense(snapshot)) {
        error = tracker.GetLastError();
        return false;
    }
    return true;
}

std::string DeleteExpenseCommand::GetName() const {
    Expense shown = snapshot;
    shown.SetID(id);
    return Describe("Delete", shown);
}

bool UpdateExpenseCommand::Execute() {
    if (!captured) {
        const Expense* current = tracker.FindExpense(after.GetID());
        if (!current) {
            error = "Expense #" + std::to_string(after.GetID()) + " no longer exists.";
            return false;
        }
        before = *current;
    }
    if (!tracker.UpdateExpense(after)) {
        error = tracker.GetLastError();
        return false;
    }
    if (!captured) {
        after = *tracker.FindExpense(after.GetID());   // keep the normalized version
        captured = true;
    }
    return true;
}

bool UpdateExpenseCommand::Undo() {
    if (!tracker.UpdateExpense(before)) {
        error = tracker.GetLastError();
        return false;
    }
    return true;
}

std::string UpdateExpenseCommand::GetName() const {
    return Describe("Edit", after);
}
