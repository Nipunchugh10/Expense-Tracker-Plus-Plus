#pragma once
#include <memory>
#include <string>
#include <vector>
#include "Expense.h"

class ExpenseTracker;

// Command pattern for undo/redo (Step 8).
//
// Scope (G10): adding, editing and deleting transactions is undoable.
// Budgets, subscriptions, goals, exchange rates, imports and resets are not;
// imports, resets and loads clear both stacks. Execute/Undo return false when
// the ledger rejects the change, and the command is then discarded.
class ICommand {
public:
    virtual ~ICommand() = default;
    virtual bool Execute() = 0;
    virtual bool Undo() = 0;
    virtual std::string GetName() const = 0;
    const std::string& GetError() const { return error; }

protected:
    std::string error;
};

class CommandManager {
public:
    explicit CommandManager(size_t maxDepth = 100) : limit(maxDepth) {}

    bool ExecuteCommand(std::unique_ptr<ICommand> cmd, std::string& error);
    bool CanUndo() const { return !undoStack.empty(); }
    bool CanRedo() const { return !redoStack.empty(); }
    bool Undo(std::string& name, std::string& error);
    bool Redo(std::string& name, std::string& error);
    std::string PeekUndoName() const;
    std::string PeekRedoName() const;
    void Clear();

private:
    size_t limit;
    std::vector<std::unique_ptr<ICommand>> undoStack;
    std::vector<std::unique_ptr<ICommand>> redoStack;
};

// Adds a transaction. Redo re-inserts it with the same ID.
class AddExpenseCommand : public ICommand {
public:
    AddExpenseCommand(ExpenseTracker& tracker, const Expense& proto) : tracker(tracker), snapshot(proto) {}
    bool Execute() override;
    bool Undo() override;
    std::string GetName() const override;
    int GetID() const { return snapshot.GetID(); }

private:
    ExpenseTracker& tracker;
    Expense snapshot;
    bool executedOnce = false;
};

// Deletes a transaction; undo restores it with its original ID and fields.
class DeleteExpenseCommand : public ICommand {
public:
    DeleteExpenseCommand(ExpenseTracker& tracker, int id) : tracker(tracker), id(id) {}
    bool Execute() override;
    bool Undo() override;
    std::string GetName() const override;

private:
    ExpenseTracker& tracker;
    int id;
    Expense snapshot;
};

// Replaces a transaction; stores before/after snapshots.
class UpdateExpenseCommand : public ICommand {
public:
    UpdateExpenseCommand(ExpenseTracker& tracker, const Expense& after) : tracker(tracker), after(after) {}
    bool Execute() override;
    bool Undo() override;
    std::string GetName() const override;

private:
    ExpenseTracker& tracker;
    Expense before;
    Expense after;
    bool captured = false;
};
