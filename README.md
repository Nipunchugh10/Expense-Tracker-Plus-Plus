# Expense Tracker Plus Plus

A native Windows desktop app for personal finance: track expenses and income, manage subscriptions, set category budgets, save toward goals and forecast your month-end spending. It is written in C++17 with Dear ImGui, ImPlot, GLFW and OpenGL 3.3, and all data stays on your computer.

---

## Download

The latest version is **[v2.0.2](https://github.com/Nipunchugh10/Expense-Tracker-Plus-Plus/releases/latest)**, with savings transactions and subscriptions found automatically in imported payments. To try it without building anything:

1. Open the [Releases page](https://github.com/Nipunchugh10/Expense-Tracker-Plus-Plus/releases/latest) and download `Expense_Tracker_Plus_Plus-v2.0.2-win64.zip`.
2. Unzip it anywhere and run `Expense_Tracker_Plus_Plus.exe`. There is no installer and nothing else to install. It needs 64-bit Windows 10 (version 2004 or later) or Windows 11. Updating keeps your data, which lives in `%APPDATA%\ExpenseTrackerPlusPlus`.

The release is not code-signed yet, so Windows SmartScreen may show "Windows protected your PC". Click **More info**, then **Run anyway**. If Smart App Control is on, Windows may block it completely (see the note under [Tests](#tests)). The release notes list the SHA-256 checksum of the zip so you can verify your download.

---

## Features

| Area | What you get |
| --- | --- |
| **Transactions** | Add, edit and delete expenses, **subscriptions**, income, transfers and **savings** (subscription payments count as spending in every total and budget; savings show as money set aside, with their own total, and never use up a budget). Filter by text, type, category, currency, date range and amount. Sortable table that handles large ledgers. Ctrl+Z / Ctrl+Y undo and redo. |
| **Subscriptions** | Recurring bills that renew every day, every 7/14/28/30/84/90/180 days, on the same date every month or year, or after any custom number of days, at a set **time (HH:MM)**. For each subscription you choose: **automatic** (each renewal is added to Expenses at its renewal time) or **manual** (due payments are listed with *Record payment* / *Skip*). From 7 days before a renewal, the Subscriptions tab and the Dashboard show *"N days remaining until next renewal"*. Also shows your monthly burn rate and annual projection; subscriptions can be paused and resumed. |
| **Multi-currency** | Every total is converted to a base currency you choose. Live exchange rates are downloaded at launch when you are online (open.er-api.com, falling back to frankfurter.dev/ECB). You can also sync on demand or edit rates by hand. Transactions in a currency with no rate are left out of totals and flagged, never silently mixed in. |
| **Budgets** | An overall monthly budget plus per-category budgets. Status is Safe (< 75%), Warning (75-99%) or Over (>= 100%), and the over-budget amount is shown. |
| **Cash flow** | Income, expenses, net cash flow and savings rate for any month or year. |
| **Goals** | Savings goals with progress, and the monthly deposit needed to reach each target date. |
| **Forecast** | Month-end projection based on your daily discretionary spending plus scheduled bills, with a warning if you will exceed your budget. |
| **Auto-categorization** | Suggests a category from keywords in the description (for example Swiggy → Dining, Uber → Transportation). You can add your own rules. |
| **Reports** | One-click monthly report as self-contained HTML or Markdown. |
| **Themes** | Dark Modern, Tokyo Night, Catppuccin, Nord Slate, OLED Black, Light Clean. |
| **Import / export** | CSV import (RFC 4180, Windows-1252 files, legacy exports, duplicate detection, empty categories filled in from your category rules) and CSV/JSON export. After an import, subscriptions hidden in the payment history are recognised and can be created with one click (also under **Tools > Find Subscriptions in Past Payments...**). |
| **Welcome screen** | On first launch, the left half introduces every feature in an animated 3-second carousel, and the right half offers **New user, fresh start** or **I have a backup**. You can reopen it from **View > Welcome Screen**. |
| **Move to a new computer** | **File > Export Full Backup...** writes one file with everything: transactions, subscriptions, budgets, goals, exchange rates, category rules and settings. On the new computer, choose **I have a backup** on the welcome screen and pick that file. |
| **Restore from backup** | **File > Restore from Backup...** opens any backup or exported JSON (current or older format). The file is read in the background and you see what's in it first (transactions, dates, subscriptions, goals, skipped records). Then you choose **Replace** (your data becomes the backup) or **Merge** (only what's missing is added: duplicates are skipped, incoming items get fresh IDs, your budgets and rates are kept). Your current data file is always copied to `expenses.before-restore-<date>.json` first. |

## Privacy

The only network request the app makes is an anonymous download of the latest USD exchange rates at launch. No personal data is sent. Turn it off with **Tools > Sync Rates on Launch**. Without internet, the app keeps using the last saved rates. The full policy is in [PRIVACY.md](PRIVACY.md).

## Code signing policy

Release v2.0.0 is **not** code-signed, so Windows may warn about it (see [Download](#download)). Code signing is planned for a later release; this section will be updated when it is in place.

- **How releases are built:** a GitHub Actions workflow (`.github/workflows/build.yml`) builds the executable from the public source code in this repository and runs the unit tests. Releases are built only from this project's own source code.
- **Team roles:** this is a one-person project. [Nipunchugh10](https://github.com/Nipunchugh10) is the author, reviewer and approver of every release. Two-factor authentication is enabled on the GitHub account.
- **Privacy:** see [Privacy](#privacy). The app sends no personal data. Its only network request is the anonymous exchange-rate download at launch, which you can turn off with **Tools > Sync Rates on Launch**.

## Data safety

- **Location.** Data is stored in `%APPDATA%\ExpenseTrackerPlusPlus\` (`expenses.json`, `category_rules.json`, `settings.json`), whichever folder you start the app from. To keep data somewhere else, set the environment variable `EXPENSE_TRACKER_DATA_DIR`. **File > Open data folder** opens the folder.
- **Coming from "Expense Tracker Plus"?** On first launch, the data in the old `%APPDATA%\ExpenseTrackerPlus` folder is copied automatically. The old folder is left untouched as a backup.
- **Atomic saves.** Every save writes a temporary file, flushes it to disk and then replaces the old file. The previous version is kept as `expenses.json.bak`. Changes also auto-save within a minute.
- **Unreadable files.** If a file can't be read, the app backs it up, disables saving and asks what to do. It never overwrites the damaged file.
- **Files from a newer version.** These open read-only, so no data is lost.
- **Upgrades.** Files from earlier versions of this app load without changes. A one-time backup (`expenses.v1.bak.json`) is made before the first upgrade.
- **Reset Everything.** A timestamped backup is written before anything is deleted.

## Large ledgers

Totals, charts, the transaction list and the forecast are recalculated only when data changes, not on every frame. CSV imports and backup restores are read on a background thread. Measured on the development laptop with **200,000 transactions** (a 49 MB file covering 2016–2026):

- The window opens immediately.
- Idle CPU is about 1% of the machine on the Dashboard, Expenses and Analytics tabs.
- Memory is about 160 MB, briefly peaking at about 380 MB during a save.
- Auto-save completes normally.

## Build and run (Windows 10/11)

Requirements: a MinGW-w64 GCC toolchain with C++17 support and CMake 3.16+ on your `PATH`. GLFW, Dear ImGui, ImPlot and nlohmann/json are bundled in `third_party/`, so nothing else needs to be installed.

```bat
git clone https://github.com/Nipunchugh10/Expense-Tracker-Plus-Plus.git Expense_Tracker_Plus_Plus
cd Expense_Tracker_Plus_Plus
run.bat
```

`run.bat` (or `run.ps1`) does an incremental build and then starts `build\bin\Expense_Tracker_Plus_Plus.exe`. If the build fails, it doesn't start the app. To build only:

```bat
build.bat
```

or by hand:

```bat
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

First-party code is compiled with `-Wall -Wextra -Wpedantic -Werror`, so any new warning fails the build.

### Tests

```bat
ctest --test-dir build --output-on-failure
```

The unit tests (`tests/`) cover dates, money precision, validation, JSON load/save (including corrupted and hostile files), atomic writes, CSV import and export, the recurring engine, currency conversion, budgets, goals, the forecast, auto-categorization, undo/redo and report escaping.

> **Note:** If Windows *Smart App Control* is on, it may block unsigned programs, both the executables you compile (the app and the test runner) and the unsigned MinGW tools themselves (`mingw32-make.exe`, `windres.exe`). Windows then shows "An Application Control policy has blocked this file" and the build fails with "unknown error". Smart App Control can only be turned off (Windows Security > App & browser control), and Windows does not allow turning it back on without a reset, so decide before you switch it off.

## Data format

`expenses.json` (schema version 2, or 3 when it contains savings) holds transactions (with `type`: `expense` / `income` / `transfer` / `subscription` / `savings`), subscriptions, overall and category budgets, goals, the base currency and exchange rates (stored against USD). Compatibility rules:

- Files without a `version` (older releases) load unchanged.
- A missing `type` means expense.
- The legacy monthly `budgets` object is still read and is also written, for older builds.
- Invalid records are skipped and listed under **Details** in the status bar. They never crash the app.

## License

MIT. See [LICENSE](LICENSE).

## Acknowledgments

- [Dear ImGui](https://github.com/ocornut/imgui) by Omar Cornut
- [ImPlot](https://github.com/epezent/implot) by Evan Pezent
- [GLFW](https://www.glfw.org/)
- [nlohmann/json](https://github.com/nlohmann/json) by Niels Lohmann
