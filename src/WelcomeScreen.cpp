#include "WelcomeScreen.h"
#include "ThemeManager.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace {

constexpr float kPi = 3.14159265f;
#define RUPEE "\xE2\x82\xB9"

struct Slide {
    const char* title;
    const char* body;
};

const Slide kSlides[] = {
    {"Track every rupee", "Log expenses, income and transfers in seconds. Search, filter and sort your history, "
                          "and undo mistakes with Ctrl+Z."},
    {"Never miss a bill", "Rent, Netflix, gym, insurance: recurring bills are recorded automatically, and you are "
                          "warned 7 days before they renew."},
    {"Spend in any currency", "Every total converts to your base currency using live exchange rates, fetched "
                              "whenever you open the app."},
    {"Budget by category", "Set monthly limits for groceries, dining or shopping, and watch them turn green, "
                           "amber or red as you spend."},
    {"See your cash flow", "Income against expenses, net savings and your savings rate, for any month or year."},
    {"Save toward goals", "An emergency fund, a trip, a new laptop: track progress and know exactly how much to "
                          "set aside each month."},
    {"Forecast your month", "See where this month will end at your current pace, and get warned before you "
                            "cross your budget."},
    {"Let it categorise for you", "Type \"Swiggy dinner\" and it is filed under Dining. Teach it your own "
                                  "merchants too."},
    {"Reports and safe backups", "One-click monthly reports in HTML or Markdown, automatic backups, and a "
                                 "one-file restore on any computer."},
    {"Make it yours", "Six themes, from Dark Modern to Tokyo Night and Light Clean. Your data never leaves "
                      "your computer."},
};
constexpr int kSlideCount = static_cast<int>(sizeof(kSlides) / sizeof(kSlides[0]));

float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
float EaseOut(float t) {
    t = Clamp01(t);
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

ImU32 Col(const ImVec4& c, float alpha) {
    ImVec4 v = c;
    v.w *= alpha;
    return ImGui::GetColorU32(v);
}

ImVec4 Hex(unsigned rgb) {
    return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 1.0f);
}

void Text(ImDrawList* dl, ImVec2 p, float size, ImU32 col, const char* s, float wrap = 0.0f) {
    dl->AddText(ImGui::GetFont(), size, p, col, s, nullptr, wrap);
}

ImVec2 TextSize(float size, const char* s, float wrap = 0.0f) {
    return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, wrap, s);
}

ImVec2 Add(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }

// The app icon (spiral notebook with a rupee sign), drawn with vectors.
void DrawLogo(ImDrawList* dl, ImVec2 p, float s) {
    const float r = s * 0.09f;
    dl->AddRectFilled(Add(p, ImVec2(s * 0.30f, s * 0.12f)), Add(p, ImVec2(s * 0.92f, s * 0.95f)),
                      ImGui::GetColorU32(Hex(0xE2E8F0)), r);
    dl->AddRectFilled(Add(p, ImVec2(s * 0.20f, s * 0.07f)), Add(p, ImVec2(s * 0.85f, s * 0.92f)),
                      ImGui::GetColorU32(Hex(0x2F6FE6)), r);
    dl->AddRectFilled(Add(p, ImVec2(s * 0.20f, s * 0.07f)), Add(p, ImVec2(s * 0.33f, s * 0.92f)),
                      ImGui::GetColorU32(Hex(0x1E3A8A)), r, ImDrawFlags_RoundCornersLeft);
    dl->AddRectFilled(Add(p, ImVec2(s * 0.68f, s * 0.07f)), Add(p, ImVec2(s * 0.77f, s * 0.28f)),
                      ImGui::GetColorU32(Hex(0x4CC38A)));
    for (int i = 0; i < 4; i++) {
        float y = s * (0.20f + i * 0.20f);
        dl->AddEllipse(Add(p, ImVec2(s * 0.20f, y)), ImVec2(s * 0.08f, s * 0.04f), ImGui::GetColorU32(Hex(0xE5E7EB)),
                       0.0f, 0, s * 0.03f);
    }
    ImVec2 ts = TextSize(s * 0.62f, RUPEE);
    Text(dl, Add(p, ImVec2(s * 0.59f - ts.x * 0.5f, s * 0.50f - ts.y * 0.5f)), s * 0.62f, IM_COL32_WHITE, RUPEE);
}

// One animated illustration per feature. `lt` is the time within the slide
// (0..3 s) and `a` the slide's fade alpha.
void DrawIllustration(int index, ImDrawList* dl, ImVec2 o, ImVec2 sz, float lt, float a) {
    const ThemeTokens& tk = ThemeManager::Tokens();
    const float U = sz.y / 10.0f;
    const float fs = ImGui::GetFontSize();
    const float enter = EaseOut(lt / 1.1f);

    switch (index) {
    case 0: {   // transactions sliding in
        struct Row { const char* name; const char* amount; ImVec4 color; };
        const Row rows[] = {{"Swiggy dinner", "-" RUPEE "420.00", tk.expense},
                            {"Salary - Acme Technologies", "+" RUPEE "92,000.00", tk.income},
                            {"BigBasket weekly order", "-" RUPEE "1,240.00", tk.expense}};
        for (int r = 0; r < 3; r++) {
            float t = EaseOut((lt - r * 0.22f) / 0.6f);
            float y = o.y + U * (0.6f + r * 3.1f);
            float x = o.x + (1.0f - t) * U * 4.0f;
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + sz.x - U * 0.5f, y + U * 2.5f), Col(tk.muted, 0.13f * a * t), U * 0.5f);
            dl->AddCircleFilled(ImVec2(x + U * 1.3f, y + U * 1.25f), U * 0.55f, Col(rows[r].color, a * t));
            Text(dl, ImVec2(x + U * 2.4f, y + U * 1.25f - fs * 0.55f), fs * 1.05f, Col(tk.text, a * t), rows[r].name);
            ImVec2 as = TextSize(fs * 1.05f, rows[r].amount);
            Text(dl, ImVec2(x + sz.x - U * 1.3f - as.x, y + U * 1.25f - fs * 0.55f), fs * 1.05f,
                 Col(rows[r].color, a * t), rows[r].amount);
        }
        break;
    }
    case 1: {   // renewal calendar
        const char* labels[] = {"Rent", "Netflix", "Gym", "Insurance"};
        const char* days[] = {"5", "12", "3", "20"};
        float w = sz.x * 0.2f, gap = sz.x * 0.05f;
        float pulse = 0.55f + 0.45f * std::sin(lt * 5.0f);
        for (int i = 0; i < 4; i++) {
            float t = EaseOut((lt - i * 0.15f) / 0.5f);
            float x = o.x + i * (w + gap) + gap * 0.4f;
            float y = o.y + U * 0.8f + (1.0f - t) * U * 1.5f;
            bool due = (i == 1);
            ImVec4 edge = due ? tk.warning : tk.muted;
            if (due) dl->AddRect(ImVec2(x - 4, y - 4), ImVec2(x + w + 4, y + U * 6.4f + 4), Col(tk.warning, 0.5f * pulse * a * t), U * 0.5f, 0, 3.0f);
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + U * 6.4f), Col(tk.muted, 0.12f * a * t), U * 0.4f);
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + U * 1.4f), Col(edge, (due ? 0.9f : 0.35f) * a * t), U * 0.4f,
                              ImDrawFlags_RoundCornersTop);
            ImVec2 ds = TextSize(fs * 2.1f, days[i]);
            Text(dl, ImVec2(x + (w - ds.x) * 0.5f, y + U * 1.9f), fs * 2.1f, Col(tk.text, a * t), days[i]);
            ImVec2 ls = TextSize(fs * 0.95f, labels[i]);
            Text(dl, ImVec2(x + (w - ls.x) * 0.5f, y + U * 4.9f), fs * 0.95f, Col(tk.muted, a * t), labels[i]);
        }
        float t = EaseOut((lt - 0.8f) / 0.5f);
        Text(dl, ImVec2(o.x + gap * 0.4f, o.y + U * 8.3f), fs * 1.05f, Col(tk.warning, a * t), "Netflix renews in 3 days");
        break;
    }
    case 2: {   // currencies flowing into the base currency
        const char* syms[] = {"$", "\xE2\x82\xAC", "\xC2\xA3"};
        ImVec2 target(o.x + sz.x * 0.72f, o.y + sz.y * 0.47f);
        float bigR = U * 2.6f;
        for (int k = 0; k < 3; k++) {
            ImVec2 c(o.x + sz.x * 0.14f, o.y + U * (1.6f + k * 3.1f));
            float t = EaseOut((lt - k * 0.15f) / 0.5f);
            dl->AddLine(c, target, Col(tk.muted, 0.35f * a * t), 2.0f);
            float ph = std::fmod(lt * 0.9f + k * 0.33f, 1.0f);
            ImVec2 dot(c.x + (target.x - c.x) * ph, c.y + (target.y - c.y) * ph);
            dl->AddCircleFilled(dot, U * 0.28f, Col(tk.accent, a * t));
            dl->AddCircleFilled(c, U * 1.2f, Col(tk.muted, 0.25f * a * t));
            ImVec2 ts = TextSize(fs * 1.5f, syms[k]);
            Text(dl, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), fs * 1.5f, Col(tk.text, a * t), syms[k]);
        }
        dl->AddCircleFilled(target, bigR * (0.8f + 0.2f * enter), Col(tk.accent, 0.9f * a));
        ImVec2 ts = TextSize(fs * 2.6f, RUPEE);
        Text(dl, ImVec2(target.x - ts.x * 0.5f, target.y - ts.y * 0.5f), fs * 2.6f, Col(ImVec4(1, 1, 1, 1), a), RUPEE);
        ImVec2 ls = TextSize(fs, "live rates");
        Text(dl, ImVec2(target.x - ls.x * 0.5f, target.y + bigR + U * 0.4f), fs, Col(tk.muted, a), "live rates");
        break;
    }
    case 3: {   // category budgets filling up
        struct Bar { const char* name; float value; ImVec4 color; const char* note; };
        const Bar bars[] = {{"Groceries", 0.45f, tk.success, "45%"},
                            {"Dining", 0.82f, tk.warning, "82%"},
                            {"Shopping", 1.04f, tk.danger, "104%  over by " RUPEE "180"}};
        float labelW = sz.x * 0.24f, trackW = sz.x * 0.46f;
        for (int i = 0; i < 3; i++) {
            float t = EaseOut((lt - i * 0.2f) / 1.0f);
            float y = o.y + U * (1.0f + i * 3.0f);
            Text(dl, ImVec2(o.x, y), fs * 1.05f, Col(tk.text, a), bars[i].name);
            ImVec2 b0(o.x + labelW, y + fs * 0.1f), b1(b0.x + trackW, b0.y + U * 1.1f);
            dl->AddRectFilled(b0, b1, Col(tk.muted, 0.2f * a), U * 0.55f);
            float fill = std::min(bars[i].value, 1.0f) * t;
            dl->AddRectFilled(b0, ImVec2(b0.x + trackW * fill, b1.y), Col(bars[i].color, a), U * 0.55f);
            Text(dl, ImVec2(b1.x + U * 0.6f, y), fs * 0.95f, Col(bars[i].color, a * t), bars[i].note);
        }
        break;
    }
    case 4: {   // income vs expenses bars
        const float inc[] = {0.62f, 0.66f, 0.80f, 0.70f, 0.74f, 0.78f};
        const float exp[] = {0.40f, 0.45f, 0.52f, 0.43f, 0.66f, 0.47f};
        const char* months[] = {"Apr", "May", "Jun", "Jul", "Aug", "Sep"};
        float chartW = sz.x * 0.66f, baseY = o.y + U * 8.6f, maxH = U * 7.4f;
        float slot = chartW / 6.0f, bw = slot * 0.3f;
        dl->AddLine(ImVec2(o.x, baseY), ImVec2(o.x + chartW, baseY), Col(tk.muted, 0.5f * a), 1.5f);
        for (int i = 0; i < 6; i++) {
            float t = EaseOut((lt - i * 0.08f) / 0.9f);
            float x = o.x + i * slot + slot * 0.18f;
            dl->AddRectFilled(ImVec2(x, baseY - maxH * inc[i] * t), ImVec2(x + bw, baseY), Col(tk.income, a), 3.0f,
                              ImDrawFlags_RoundCornersTop);
            dl->AddRectFilled(ImVec2(x + bw + 3, baseY - maxH * exp[i] * t), ImVec2(x + 2 * bw + 3, baseY),
                              Col(tk.expense, a), 3.0f, ImDrawFlags_RoundCornersTop);
            Text(dl, ImVec2(x, baseY + U * 0.3f), fs * 0.85f, Col(tk.muted, a), months[i]);
        }
        char rate[32];
        std::snprintf(rate, sizeof(rate), "%d%%", static_cast<int>(36.0f * enter + 0.5f));
        float rx = o.x + chartW + U * 1.2f;
        Text(dl, ImVec2(rx, o.y + U * 2.0f), fs * 0.95f, Col(tk.muted, a), "Savings rate");
        Text(dl, ImVec2(rx, o.y + U * 3.3f), fs * 2.4f, Col(tk.success, a), rate);
        break;
    }
    case 5: {   // goal progress ring
        ImVec2 c(o.x + U * 3.8f, o.y + sz.y * 0.47f);
        float r = U * 3.2f;
        dl->AddCircle(c, r, Col(tk.muted, 0.25f * a), 64, U * 0.7f);
        float frac = 0.70f * enter;
        dl->PathArcTo(c, r, -kPi * 0.5f, -kPi * 0.5f + 2.0f * kPi * frac, 64);
        dl->PathStroke(Col(tk.success, a), 0, U * 0.7f);
        char pct[16];
        std::snprintf(pct, sizeof(pct), "%d%%", static_cast<int>(frac * 100.0f + 0.5f));
        ImVec2 ps = TextSize(fs * 1.9f, pct);
        Text(dl, ImVec2(c.x - ps.x * 0.5f, c.y - ps.y * 0.5f), fs * 1.9f, Col(tk.text, a), pct);
        float tx = c.x + r + U * 1.6f;
        Text(dl, ImVec2(tx, o.y + U * 2.2f), fs * 1.35f, Col(tk.text, a), "Emergency Fund");
        Text(dl, ImVec2(tx, o.y + U * 4.0f), fs * 1.0f, Col(tk.muted, a), RUPEE "2,10,000 of " RUPEE "3,00,000");
        float t = EaseOut((lt - 0.9f) / 0.5f);
        Text(dl, ImVec2(tx, o.y + U * 5.6f), fs * 1.0f, Col(tk.accent, a * t), "Save " RUPEE "15,000/mo to finish by March");
        break;
    }
    case 6: {   // month-end forecast
        ImVec2 c0(o.x, o.y + U * 0.4f), c1(o.x + sz.x * 0.86f, o.y + U * 9.2f);
        dl->AddRectFilled(c0, c1, Col(tk.muted, 0.08f * a), U * 0.3f);
        float budgetY = c0.y + (c1.y - c0.y) * 0.28f;
        dl->AddLine(ImVec2(c0.x, budgetY), ImVec2(c1.x, budgetY), Col(tk.danger, 0.9f * a), 2.0f);
        Text(dl, ImVec2(c0.x + U * 0.4f, budgetY - fs * 1.2f), fs * 0.85f, Col(tk.danger, a), "Budget");
        const int n = 12;
        auto pt = [&](int i) {
            float x = c0.x + (c1.x - c0.x) * (static_cast<float>(i) / 30.0f);
            float v = static_cast<float>(i) / 30.0f * 1.12f;
            return ImVec2(x, c1.y - (c1.y - c0.y) * v * 0.82f);
        };
        ImVec2 pts[n + 1];
        int shown = std::max(1, static_cast<int>(n * enter));
        for (int i = 0; i <= shown; i++) pts[i] = pt(i);
        dl->AddPolyline(pts, shown + 1, Col(tk.accent, a), 0, 3.0f);
        float ft = EaseOut((lt - 1.0f) / 1.0f);
        for (int i = n; i < n + static_cast<int>(18 * ft); i += 2) dl->AddLine(pt(i), pt(i + 1), Col(tk.warning, a), 2.5f);
        if (ft > 0.95f) {
            ImVec2 hit = pt(23);
            dl->AddCircleFilled(ImVec2(hit.x, budgetY), U * 0.35f, Col(tk.danger, a));
            Text(dl, ImVec2(hit.x - U * 5.0f, budgetY + U * 0.5f), fs * 0.9f, Col(tk.danger, a), "over budget by the 23rd");
        }
        break;
    }
    case 7: {   // auto-categorisation
        const char* typed = "Swiggy dinner";
        int len = static_cast<int>(std::strlen(typed));
        int count = std::min(len, static_cast<int>(len * Clamp01(lt / 1.1f)));
        std::string shown(typed, static_cast<size_t>(count));
        ImVec2 b0(o.x, o.y + U * 2.6f), b1(o.x + sz.x * 0.46f, o.y + U * 5.4f);
        dl->AddRectFilled(b0, b1, Col(tk.muted, 0.14f * a), U * 0.4f);
        dl->AddRect(b0, b1, Col(tk.accent, 0.6f * a), U * 0.4f, 0, 1.5f);
        Text(dl, ImVec2(b0.x + U * 0.6f, b0.y - fs * 1.4f), fs * 0.85f, Col(tk.muted, a), "Description");
        Text(dl, ImVec2(b0.x + U * 0.7f, (b0.y + b1.y) * 0.5f - fs * 0.6f), fs * 1.15f, Col(tk.text, a), shown.c_str());
        if (std::fmod(lt, 0.6f) < 0.35f && count < len) {
            float cx = b0.x + U * 0.7f + TextSize(fs * 1.15f, shown.c_str()).x + 2;
            dl->AddLine(ImVec2(cx, b0.y + U * 0.7f), ImVec2(cx, b1.y - U * 0.7f), Col(tk.text, a), 1.5f);
        }
        float t = EaseOut((lt - 1.3f) / 0.45f);
        if (t > 0.0f) {
            float ax = b1.x + U * 0.8f, ay = (b0.y + b1.y) * 0.5f;
            dl->AddLine(ImVec2(ax, ay), ImVec2(ax + U * 2.2f * t, ay), Col(tk.muted, a), 2.0f);
            dl->AddTriangleFilled(ImVec2(ax + U * 2.2f * t + 6, ay), ImVec2(ax + U * 2.2f * t - 2, ay - 5),
                                  ImVec2(ax + U * 2.2f * t - 2, ay + 5), Col(tk.muted, a));
            float chipX = ax + U * 3.2f;
            ImVec2 cs = TextSize(fs * 1.1f, "Dining");
            float pad = U * 0.7f, grow = 0.6f + 0.4f * t;
            ImVec2 c0(chipX, ay - (cs.y * 0.5f + pad * 0.6f) * grow), c1(chipX + (cs.x + pad * 2) * grow, ay + (cs.y * 0.5f + pad * 0.6f) * grow);
            dl->AddRectFilled(c0, c1, Col(tk.success, 0.25f * a * t), U * 1.0f);
            Text(dl, ImVec2(c0.x + pad * grow, ay - cs.y * 0.5f), fs * 1.1f, Col(tk.success, a * t), "Dining");
            Text(dl, ImVec2(b0.x, b1.y + U * 0.8f), fs * 0.9f, Col(tk.muted, a * t), "matched keyword \"swiggy\"");
        }
        break;
    }
    case 8: {   // report + backup check
        ImVec2 d0(o.x + U * 0.5f, o.y + U * 0.4f), d1(d0.x + U * 6.0f, d0.y + U * 8.2f);
        dl->AddRectFilled(d0, d1, Col(tk.muted, 0.18f * a), U * 0.3f);
        dl->AddTriangleFilled(ImVec2(d1.x - U * 1.6f, d0.y), ImVec2(d1.x, d0.y), ImVec2(d1.x, d0.y + U * 1.6f),
                              Col(tk.accent, 0.7f * a));
        for (int i = 0; i < 6; i++) {
            float t = EaseOut((lt - i * 0.12f) / 0.4f);
            float y = d0.y + U * (2.2f + i * 0.95f);
            float w = (i % 3 == 2 ? 2.8f : 4.2f) * U * t;
            dl->AddRectFilled(ImVec2(d0.x + U * 0.8f, y), ImVec2(d0.x + U * 0.8f + w, y + U * 0.35f), Col(tk.text, 0.55f * a), 2.0f);
        }
        Text(dl, ImVec2(d0.x, d1.y + U * 0.3f), fs * 0.85f, Col(tk.muted, a), "report-2026-09.html");
        ImVec2 c(o.x + sz.x * 0.62f, o.y + sz.y * 0.42f);
        float r = U * 2.6f;
        float t = EaseOut((lt - 0.6f) / 0.6f);
        dl->AddCircleFilled(c, r * (0.85f + 0.15f * t), Col(tk.success, 0.22f * a * t));
        dl->AddCircle(c, r * (0.85f + 0.15f * t), Col(tk.success, a * t), 48, 3.0f);
        float ct = EaseOut((lt - 1.0f) / 0.5f);
        if (ct > 0.0f) {
            ImVec2 p1(c.x - r * 0.45f, c.y), p2(c.x - r * 0.1f, c.y + r * 0.38f), p3(c.x + r * 0.5f, c.y - r * 0.35f);
            float seg = std::min(ct * 2.0f, 1.0f);
            dl->AddLine(p1, ImVec2(p1.x + (p2.x - p1.x) * seg, p1.y + (p2.y - p1.y) * seg), Col(tk.success, a), 4.0f);
            if (ct > 0.5f) {
                float s2 = (ct - 0.5f) * 2.0f;
                dl->AddLine(p2, ImVec2(p2.x + (p3.x - p2.x) * s2, p2.y + (p3.y - p2.y) * s2), Col(tk.success, a), 4.0f);
            }
        }
        ImVec2 ls = TextSize(fs * 0.95f, "Backup saved");
        Text(dl, ImVec2(c.x - ls.x * 0.5f, c.y + r + U * 0.5f), fs * 0.95f, Col(tk.success, a * t), "Backup saved");
        break;
    }
    case 9: {   // themes
        struct Swatch { unsigned bg, surface, accent; const char* name; };
        const Swatch sw[] = {{0x16171A, 0x2A2D33, 0x4C9AFF, "Dark Modern"}, {0x1A1B26, 0x292E42, 0x7AA2F7, "Tokyo Night"},
                             {0x1E1E2E, 0x363A4F, 0xCBA6F7, "Catppuccin"},  {0x2E3440, 0x434C5E, 0x88C0D0, "Nord Slate"},
                             {0x000000, 0x1C1C1C, 0x3D8BFD, "OLED Black"},  {0xF6F7F9, 0xE8EBF0, 0x2563EB, "Light Clean"}};
        float w = sz.x * 0.27f, h = U * 3.6f, gx = sz.x * 0.05f;
        for (int i = 0; i < 6; i++) {
            float t = EaseOut((lt - i * 0.12f) / 0.45f);
            int col = i % 3, row = i / 3;
            float x = o.x + col * (w + gx), y = o.y + U * 0.3f + row * (h + U * 1.4f);
            float shrink = (1.0f - t) * w * 0.15f;
            ImVec2 p0(x + shrink, y + shrink), p1(x + w - shrink, y + h - shrink);
            dl->AddRectFilled(p0, p1, Col(Hex(sw[i].bg), a * t), U * 0.4f);
            dl->AddRect(p0, p1, Col(tk.muted, 0.35f * a * t), U * 0.4f);
            dl->AddRectFilled(ImVec2(p0.x + U * 0.5f, p0.y + U * 0.6f), ImVec2(p1.x - U * 0.5f, p0.y + U * 1.3f),
                              Col(Hex(sw[i].surface), a * t), 3.0f);
            dl->AddRectFilled(ImVec2(p0.x + U * 0.5f, p0.y + U * 1.8f), ImVec2(p0.x + (p1.x - p0.x) * 0.55f, p0.y + U * 2.5f),
                              Col(Hex(sw[i].accent), a * t), 3.0f);
            Text(dl, ImVec2(x, y + h + U * 0.2f), fs * 0.85f, Col(tk.muted, a * t), sw[i].name);
        }
        break;
    }
    default:
        break;
    }
}

bool CardButton(const char* id, int icon, const char* title, const char* body, float width) {
    const ThemeTokens& tk = ThemeManager::Tokens();
    const float fs = ImGui::GetFontSize();
    const float pad = fs * 1.2f, iconArea = fs * 4.2f;
    const float textW = width - iconArea - pad * 2.2f;
    const float titleSize = fs * 1.3f, bodySize = fs * 0.95f;
    const float bodyH = TextSize(bodySize, body, textW).y;
    const float height = pad * 2.0f + titleSize * 1.25f + bodyH;

    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, ImVec2(width, height));
    bool hovered = ImGui::IsItemHovered();
    bool held = ImGui::IsItemActive();
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 q(p.x + width, p.y + height);
    float rounding = fs * 0.6f;
    dl->AddRectFilled(p, q, Col(tk.accent, held ? 0.26f : hovered ? 0.17f : 0.08f), rounding);
    dl->AddRect(p, q, Col(tk.accent, hovered ? 0.9f : 0.35f), rounding, 0, hovered ? 2.0f : 1.2f);

    ImVec2 ic(p.x + pad + fs * 1.6f, p.y + height * 0.5f);
    float ir = fs * 1.5f;
    dl->AddCircleFilled(ic, ir, Col(tk.accent, 0.22f));
    ImU32 glyph = Col(tk.accent, 1.0f);
    if (icon == 0) {   // plus / fresh start
        dl->AddLine(ImVec2(ic.x - ir * 0.45f, ic.y), ImVec2(ic.x + ir * 0.45f, ic.y), glyph, 3.0f);
        dl->AddLine(ImVec2(ic.x, ic.y - ir * 0.45f), ImVec2(ic.x, ic.y + ir * 0.45f), glyph, 3.0f);
    } else {           // box with an arrow coming in / restore
        dl->AddRect(ImVec2(ic.x - ir * 0.5f, ic.y - ir * 0.05f), ImVec2(ic.x + ir * 0.5f, ic.y + ir * 0.5f), glyph, 2.0f, 0, 2.5f);
        dl->AddLine(ImVec2(ic.x, ic.y - ir * 0.6f), ImVec2(ic.x, ic.y + ir * 0.2f), glyph, 2.5f);
        dl->AddTriangleFilled(ImVec2(ic.x, ic.y + ir * 0.32f), ImVec2(ic.x - ir * 0.22f, ic.y + ir * 0.05f),
                              ImVec2(ic.x + ir * 0.22f, ic.y + ir * 0.05f), glyph);
    }

    float tx = p.x + iconArea + pad * 0.4f;
    Text(dl, ImVec2(tx, p.y + pad), titleSize, Col(tk.text, 1.0f), title);
    Text(dl, ImVec2(tx, p.y + pad + titleSize * 1.25f), bodySize, Col(tk.muted, 1.0f), body, textW);

    float cx = q.x - pad * 0.9f + (hovered ? fs * 0.25f : 0.0f), cy = p.y + height * 0.5f;
    dl->AddLine(ImVec2(cx - fs * 0.35f, cy - fs * 0.45f), ImVec2(cx, cy), Col(tk.text, 0.7f), 2.5f);
    dl->AddLine(ImVec2(cx - fs * 0.35f, cy + fs * 0.45f), ImVec2(cx, cy), Col(tk.text, 0.7f), 2.5f);
    return clicked;
}

} // namespace

WelcomeScreen::Action WelcomeScreen::Render(bool hasExistingData) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float left = std::floor(avail.x * 0.5f);
    RenderShowcase(left, avail.y);
    ImGui::SameLine(0.0f, 0.0f);
    return RenderChoices(avail.x - left, avail.y, hasExistingData);
}

void WelcomeScreen::RenderShowcase(float width, float height) {
    const ThemeTokens& tk = ThemeManager::Tokens();
    const float fs = ImGui::GetFontSize();
    const float base = ImGui::GetStyle().FontSizeBase;

    ImGui::BeginChild("##WelcomeShowcase", ImVec2(width, height), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    dl->AddRectFilledMultiColor(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), Col(tk.accent, 0.16f), Col(tk.accent, 0.05f),
                                Col(tk.accent, 0.0f), Col(tk.accent, 0.09f));

    const float margin = fs * 2.6f;
    const float contentW = ws.x - margin * 2.0f;
    ImVec2 cur(wp.x + margin, wp.y + margin);

    // Header: logo + app name
    float logo = fs * 3.2f;
    DrawLogo(dl, cur, logo);
    Text(dl, ImVec2(cur.x + logo + fs * 0.8f, cur.y + logo * 0.5f - fs * 0.75f), fs * 1.45f, Col(tk.text, 1.0f),
         "Expense Tracker Plus Plus");
    cur.y += logo + fs * 1.6f;

    Text(dl, cur, base * 2.9f, Col(tk.text, 1.0f), "Welcome!");
    cur.y += base * 2.9f * 1.25f;
    const char* tagline = "Your money, clearly: private, offline-first personal finance for your desktop.";
    Text(dl, cur, fs * 1.1f, Col(tk.muted, 1.0f), tagline, contentW);
    cur.y += TextSize(fs * 1.1f, tagline, contentW).y + fs * 2.2f;

    Text(dl, cur, fs * 1.3f, Col(tk.muted, 1.0f), "With Expense Tracker Plus Plus, you can");
    cur.y += fs * 1.3f * 1.6f;

    // Slide area (hovering pauses the carousel)
    const float footerH = fs * 3.0f;
    ImVec2 slideMin = cur, slideMax(wp.x + ws.x - margin, wp.y + ws.y - margin - footerH);
    bool hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(slideMin, slideMax);
    if (!hovered) clock += ImGui::GetIO().DeltaTime;
    clock = std::fmod(clock, static_cast<double>(kSlideSeconds) * kSlideCount);
    const int index = static_cast<int>(clock / kSlideSeconds) % kSlideCount;
    const float lt = static_cast<float>(std::fmod(clock, static_cast<double>(kSlideSeconds)));

    const float fadeIn = EaseOut(lt / 0.35f);
    const float fadeOut = EaseOut((kSlideSeconds - lt) / 0.3f);
    const float a = std::min(fadeIn, fadeOut);
    const float yOff = (1.0f - fadeIn) * fs * 0.9f - (1.0f - fadeOut) * fs * 0.5f;

    const Slide& s = kSlides[index];
    float y = slideMin.y + yOff;
    Text(dl, ImVec2(slideMin.x, y), base * 2.2f, Col(tk.accent, a), s.title);
    y += base * 2.2f * 1.3f;
    Text(dl, ImVec2(slideMin.x, y), fs * 1.1f, Col(tk.text, 0.9f * a), s.body, contentW);
    y += TextSize(fs * 1.1f, s.body, contentW).y + fs * 1.6f;

    float illH = std::min(slideMax.y - y, fs * 13.0f);
    float illW = std::min(contentW, fs * 32.0f);
    if (illH > fs * 5.0f) DrawIllustration(index, dl, ImVec2(slideMin.x, y), ImVec2(illW, illH), lt, a);

    // Footer: progress dots (clickable) and the 3-second timer bar
    float dotR = fs * 0.28f, gap = fs * 0.9f;
    ImVec2 dp(wp.x + margin, wp.y + ws.y - margin - footerH * 0.55f);
    for (int i = 0; i < kSlideCount; i++) {
        ImVec2 c(dp.x + dotR + i * gap, dp.y);
        bool active = i == index;
        dl->AddCircleFilled(c, active ? dotR * 1.25f : dotR, Col(active ? tk.accent : tk.muted, active ? 1.0f : 0.45f));
        ImGui::SetCursorScreenPos(ImVec2(c.x - gap * 0.5f, c.y - gap * 0.5f));
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##dot", ImVec2(gap, gap))) clock = i * static_cast<double>(kSlideSeconds) + 0.001;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kSlides[i].title);
        ImGui::PopID();
    }
    float barW = std::min(contentW, fs * 16.0f);
    ImVec2 b0(dp.x, dp.y + fs * 1.0f), b1(dp.x + barW, dp.y + fs * 1.0f + 3.0f);
    dl->AddRectFilled(b0, b1, Col(tk.muted, 0.25f), 2.0f);
    dl->AddRectFilled(b0, ImVec2(b0.x + barW * (lt / kSlideSeconds), b1.y), Col(tk.accent, 0.9f), 2.0f);

    ImGui::EndChild();
}

WelcomeScreen::Action WelcomeScreen::RenderChoices(float width, float height, bool hasExistingData) {
    const ThemeTokens& tk = ThemeManager::Tokens();
    const float fs = ImGui::GetFontSize();
    const float base = ImGui::GetStyle().FontSizeBase;
    Action action = Action::None;

    ImGui::BeginChild("##WelcomeChoices", ImVec2(width, height), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (hasExistingData) {
        const char* back = "Back to my data";
        float bw = ImGui::CalcTextSize(back).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SetCursorPos(ImVec2(width - bw - fs * 1.5f, fs * 1.2f));
        if (ImGui::Button(back)) action = Action::Close;
    }

    const float colW = std::min(width - fs * 5.0f, fs * 30.0f);
    const float x = (width - colW) * 0.5f;
    ImGui::SetCursorPos(ImVec2(x, std::max(fs * 3.5f, height * 0.5f - fs * 13.0f)));

    ImGui::PushFont(nullptr, base * 1.75f);
    ImGui::TextUnformatted("Let's get you set up");
    ImGui::PopFont();
    ImGui::SetCursorPosX(x);
    ImGui::PushStyleColor(ImGuiCol_Text, tk.muted);
    ImGui::TextUnformatted("Choose how you would like to begin.");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, fs * 1.4f));

    ImGui::SetCursorPosX(x);
    const char* freshBody = hasExistingData
        ? "Start over with an empty ledger. Your current data is backed up first, so nothing is lost."
        : "Start with an empty ledger and add your first expense in seconds. You can import a CSV any time.";
    if (CardButton("##fresh", 0, "New user, fresh start", freshBody, colW)) action = Action::FreshStart;
    ImGui::Dummy(ImVec2(0, fs * 0.9f));

    ImGui::SetCursorPosX(x);
    if (CardButton("##backup", 1, "I have a backup",
                   "Moving from another computer? Pick your backup file and everything comes back exactly as it "
                   "was: transactions, subscriptions, budgets, goals, exchange rates, category rules and your theme.",
                   colW)) {
        action = Action::RestoreBackup;
    }
    ImGui::Dummy(ImVec2(0, fs * 1.4f));

    ImGui::SetCursorPosX(x);
    ImGui::PushTextWrapPos(x + colW);
    ImGui::PushStyleColor(ImGuiCol_Text, tk.muted);
    ImGui::TextWrapped("No backup yet? On your old computer choose File > Export Full Backup... and copy that single "
                       "file here. A plain expenses.json or .bak file works too.");
    ImGui::Dummy(ImVec2(0, fs * 0.6f));
    ImGui::SetCursorPosX(x);
    ImGui::TextWrapped("Your data stays on this computer.");
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();

    ImGui::EndChild();
    return action;
}
