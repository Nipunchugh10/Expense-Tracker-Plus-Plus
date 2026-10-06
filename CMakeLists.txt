cmake_minimum_required(VERSION 3.16)

project(Expense_Tracker_Plus_Plus LANGUAGES C CXX RC)

# Expense Tracker Plus Plus targets Windows 10/11 only (native Win32 dialogs,
# single-instance mutex, %APPDATA% storage).
if(NOT WIN32)
    message(FATAL_ERROR "Expense Tracker Plus Plus supports Windows 10/11 only.")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Must be set before any target is created to take effect.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ── GLFW (from source, no extras) ──────────────────────────────────
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glfw)

# ── Dear ImGui (static library) ────────────────────────────────────
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw)

# ── ImPlot (static library) ────────────────────────────────────────
set(IMPLOT_DIR ${CMAKE_SOURCE_DIR}/third_party/implot)
add_library(implot STATIC
    ${IMPLOT_DIR}/implot.cpp
    ${IMPLOT_DIR}/implot_items.cpp
)
target_include_directories(implot PUBLIC ${IMPLOT_DIR})
target_link_libraries(implot PUBLIC imgui)

# ── nlohmann/json (header-only) ────────────────────────────────────
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE ${CMAKE_SOURCE_DIR}/third_party)

# Warnings are errors for first-party code only (P0-F6).
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(APP_WARNINGS -Wall -Wextra -Wpedantic -Werror)
elseif(MSVC)
    set(APP_WARNINGS /W4 /WX /utf-8)
endif()

# ── Core library: all non-UI logic, shared by the app and the tests ─
add_library(tracker_core STATIC
    src/Money.cpp
    src/Utils.cpp
    src/Validation.cpp
    src/Expense.cpp
    src/RecurringRule.cpp
    src/Budget.cpp
    src/Goal.cpp
    src/CurrencyManager.cpp
    src/FilterCriteria.cpp
    src/ExpenseTracker.cpp
    src/AtomicFile.cpp
    src/Paths.cpp
    src/JsonIO.cpp
    src/CsvIO.cpp
    src/AutoCategorizer.cpp
    src/CommandManager.cpp
    src/ReportGenerator.cpp
    src/Settings.cpp
    src/RateSync.cpp
    src/SubscriptionDetector.cpp
)
target_include_directories(tracker_core PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(tracker_core PUBLIC nlohmann_json shell32 ole32 uuid winhttp)
target_compile_options(tracker_core PRIVATE ${APP_WARNINGS})

# ── Application ────────────────────────────────────────────────────
add_executable(${PROJECT_NAME} WIN32
    src/main.cpp
    src/App.cpp
    src/ThemeManager.cpp
    src/UiHelpers.cpp
    src/WelcomeScreen.cpp
    src/Tabs/DashboardTab.cpp
    src/Tabs/ExpensesTab.cpp
    src/Tabs/SubscriptionsTab.cpp
    src/Tabs/BudgetTab.cpp
    src/Tabs/GoalsTab.cpp
    src/Tabs/AnalyticsTab.cpp
    resources/app.rc                # icon (rupee notebook) + version info
)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/resources)
# The resource file embeds the notices and reads the version from AppInfo.h: rebuild it when they change.
set_source_files_properties(resources/app.rc PROPERTIES
    OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.txt;${CMAKE_SOURCE_DIR}/include/AppInfo.h")
target_link_libraries(${PROJECT_NAME} PRIVATE tracker_core imgui implot opengl32 gdi32 comdlg32 user32)
target_compile_options(${PROJECT_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${APP_WARNINGS}>)
if(MINGW)
    target_link_options(${PROJECT_NAME} PRIVATE -static)
endif()

# ── Tests (P0-F7): ctest --test-dir build ──────────────────────────
enable_testing()
add_executable(ExpenseTrackerPlusPlusTests
    tests/test_main.cpp
    tests/test_core.cpp
    tests/test_io.cpp
    tests/test_features.cpp
)
target_include_directories(ExpenseTrackerPlusPlusTests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_definitions(ExpenseTrackerPlusPlusTests PRIVATE FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
target_link_libraries(ExpenseTrackerPlusPlusTests PRIVATE tracker_core)
target_compile_options(ExpenseTrackerPlusPlusTests PRIVATE ${APP_WARNINGS})
if(MINGW)
    target_link_options(ExpenseTrackerPlusPlusTests PRIVATE -static)
endif()
add_test(NAME unit_tests COMMAND ExpenseTrackerPlusPlusTests)
