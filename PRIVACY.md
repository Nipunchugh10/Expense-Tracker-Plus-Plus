# Privacy Policy: Expense Tracker Plus Plus

*Effective 4 October 2026. Publisher: Nipun Chugh ([GitHub: Nipunchugh10](https://github.com/Nipunchugh10)).*

Expense Tracker Plus Plus is a personal-finance app that works on your own computer. **I do not collect, receive, sell or share your data.** There are no accounts, no advertising and no analytics or tracking in the app.

## What the app stores, and where

Everything you enter (transactions, subscriptions, budgets, goals, exchange rates, category rules and settings) is stored **only on your device**:

- Version downloaded from GitHub: in `%APPDATA%\ExpenseTrackerPlusPlus` (or the folder named by the `EXPENSE_TRACKER_DATA_DIR` environment variable).
- Version installed from the Microsoft Store: in `Documents\Expense Tracker Plus Plus`. On its first run it copies any data the downloaded version already has, and leaves the original where it is.

**File > Open data folder** opens the folder in use. If Windows backs up your Documents folder to OneDrive (a Windows setting you control), the Store version's data file is included in that backup like any other document; the app itself never uploads anything.

The app never asks for bank, card, login, PIN or tax details. You type your own amounts and descriptions, and nothing is connected to a bank.

The data files are ordinary files protected by your Windows account's permissions; the app does not encrypt them. For stronger protection, use Windows disk encryption (BitLocker or Device Encryption).

## What leaves your device

The app makes **one kind of network request**: when it starts (and when you choose **Tools > Sync Exchange Rates Now**) it downloads the latest public exchange rates.

- It is a plain `GET` request to `open.er-api.com` (ExchangeRate-API), or, if that fails, to `api.frankfurter.dev` (European Central Bank reference rates).
- The request contains **none of your data**: no amounts, descriptions, categories or any other ledger information. Like any web request, the server can see your IP address and the app's name and version (`ExpenseTrackerPlusPlus/1.0`). Those services have their own privacy policies.
- You can turn it off with **Tools > Sync Rates on Launch**. Without internet, the app keeps using the last saved rates.

## Backups and exports

CSV, JSON, report and full-backup files are created only when you choose to export, and are saved where you choose. They are not encrypted. Keep them somewhere safe.

## Microsoft Store

If you install the app from the Microsoft Store, Microsoft may collect installation and diagnostic information under its own privacy statement. The publisher only sees aggregate Store reports (for example, number of acquisitions and crash reports) in Partner Center.

## Deleting your data

Uninstalling the app (Store version) or deleting the downloaded files does **not** delete your data, so you can reinstall without losing anything. To delete your data, delete the data folder listed above (**File > Open data folder** opens it). Use **File > Export Full Backup** first if you want to keep a copy.

## Children

The app is not directed at children and collects no personal information from anyone.

## Changes and contact

If this policy changes, the new version will be published at this address with a new effective date. Questions or concerns: open an issue at <https://github.com/Nipunchugh10/Expense-Tracker-Plus-Plus/issues>.
