#include "App.h"
#include "Paths.h"
#include "ThemeManager.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <filesystem>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

namespace {

const char* const kWindowTitle = "Expense Tracker Plus Plus";
float g_pendingDpi = 0.0f;

void OnContentScale(GLFWwindow*, float xscale, float) {
    g_pendingDpi = xscale;
}

#ifdef _WIN32
std::filesystem::path WindowsFontsDir() {
    wchar_t buf[MAX_PATH] = {};
    UINT n = GetWindowsDirectoryW(buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return std::filesystem::path(L"C:\\Windows\\Fonts");
    return std::filesystem::path(buf) / L"Fonts";
}
#endif

// Segoe UI covers Latin, the currency signs (₹ € £ ¥) and most symbols; Nirmala UI
// adds Devanagari and other Indic scripts. ImGui 1.92 loads glyphs on demand, so
// no glyph-range tables are needed (P0-E6).
void LoadFonts(ImGuiIO& io) {
    const float size = 18.0f;
#ifdef _WIN32
    const std::filesystem::path dir = WindowsFontsDir();
    std::error_code ec;
    ImFont* main = nullptr;
    const std::filesystem::path segoe = dir / L"segoeui.ttf";
    if (std::filesystem::exists(segoe, ec)) {
        main = io.Fonts->AddFontFromFileTTF(Paths::ToUtf8(segoe).c_str(), size);
    }
    if (main) {
        for (const wchar_t* extra : {L"Nirmala.ttc", L"Nirmala.ttf", L"seguisym.ttf"}) {
            const std::filesystem::path p = dir / extra;
            if (!std::filesystem::exists(p, ec)) continue;
            ImFontConfig cfg;
            cfg.MergeMode = true;
            io.Fonts->AddFontFromFileTTF(Paths::ToUtf8(p).c_str(), size, &cfg);
        }
        return;
    }
#endif
    ImFontConfig cfg;
    cfg.SizePixels = size;
    io.Fonts->AddFontDefaultVector(&cfg);
}

} // namespace

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // Single instance: a second copy would autosave over the first (P0-A12).
    HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\ExpenseTrackerPlusPlus.SingleInstance");
    if (instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(nullptr, L"Expense Tracker Plus Plus")) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        } else {
            MessageBoxW(nullptr, L"Expense Tracker Plus Plus is already running.", L"Expense Tracker Plus Plus",
                        MB_OK | MB_ICONINFORMATION);
        }
        CloseHandle(instanceMutex);
        return 0;
    }
#else
int main() {
#endif
    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

    // Size the window to 85% of the monitor's work area, then centre it there
    // (the OS default position can push the bottom of the window off-screen).
    int areaX = 0, areaY = 0, areaW = 1600, areaH = 900;
    int ww = 1600, wh = 900;
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (monitor) {
        glfwGetMonitorWorkarea(monitor, &areaX, &areaY, &areaW, &areaH);
        float sx = 1.0f, sy = 1.0f;
        glfwGetMonitorContentScale(monitor, &sx, &sy);
        ww = static_cast<int>(static_cast<float>(areaW) * 0.85f / std::max(sx, 1.0f));
        wh = static_cast<int>(static_cast<float>(areaH) * 0.85f / std::max(sy, 1.0f));
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);   // position first, then show (no jump)
    GLFWwindow* window = glfwCreateWindow(std::max(ww, 960), std::max(wh, 600), kWindowTitle, nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    if (monitor) {
        int winW = 0, winH = 0, left = 0, top = 0, right = 0, bottom = 0;
        glfwGetWindowSize(window, &winW, &winH);               // already scaled to the monitor
        glfwGetWindowFrameSize(window, &left, &top, &right, &bottom);
        int outerW = winW + left + right, outerH = winH + top + bottom;
        if (outerW > areaW || outerH > areaH) {                 // tiny screens: fit inside
            winW = std::min(winW, areaW - left - right);
            winH = std::min(winH, areaH - top - bottom);
            glfwSetWindowSize(window, winW, winH);
            outerW = winW + left + right;
            outerH = winH + top + bottom;
        }
        glfwSetWindowPos(window, areaX + (areaW - outerW) / 2 + left, areaY + (areaH - outerH) / 2 + top);
    }
    glfwShowWindow(window);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetWindowContentScaleCallback(window, OnContentScale);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;   // layout is fixed; avoid writing imgui.ini into the working directory

    LoadFonts(io);
    float xs = 1.0f, ys = 1.0f;
    glfwGetWindowContentScale(window, &xs, &ys);
    ThemeManager::SetDpiScale(xs);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    App app;
    app.Init(window);

    for (;;) {
        if (glfwWindowShouldClose(window)) {
            std::string error;
            if (app.SaveBeforeExit(error)) break;
#ifdef _WIN32
            std::wstring text = L"Your latest changes could not be saved.\n\n" +
                                Paths::FromUtf8(error).wstring() +
                                L"\n\nTry again to retry saving, Continue to quit without saving, or Cancel to keep working.";
            int choice = MessageBoxW(glfwGetWin32Window(window), text.c_str(), L"Expense Tracker Plus Plus",
                                     MB_CANCELTRYCONTINUE | MB_ICONWARNING | MB_DEFBUTTON1);
            if (choice == IDTRYAGAIN) continue;
            if (choice == IDCONTINUE) break;
#endif
            glfwSetWindowShouldClose(window, GLFW_FALSE);
        }

        glfwPollEvents();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
            glfwWaitEventsTimeout(0.25);
            continue;
        }
        if (g_pendingDpi > 0.0f) {
            ThemeManager::SetDpiScale(g_pendingDpi);
            g_pendingDpi = 0.0f;
        }
        app.PreFrame();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        app.Render();

        ImGui::Render();
        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        glClearColor(bg.x, bg.y, bg.z, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
#ifdef _WIN32
    if (instanceMutex) {
        ReleaseMutex(instanceMutex);
        CloseHandle(instanceMutex);
    }
#endif
    return 0;
}
