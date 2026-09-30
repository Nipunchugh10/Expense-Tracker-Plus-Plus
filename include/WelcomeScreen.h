#pragma once

// First-run welcome screen.
// Left half: welcome message and a carousel of the app's features, each shown
// for 3 seconds with a short entrance animation. Right half: "New user, fresh
// start" and "I have a backup". Also reachable from View > Welcome Screen.
class WelcomeScreen {
public:
    enum class Action { None, FreshStart, RestoreBackup, Close };

    // hasExistingData: opened from the menu with data already loaded; shows a
    // way back and warns that a fresh start replaces the current data.
    Action Render(bool hasExistingData);

    static constexpr float kSlideSeconds = 3.0f;

private:
    void RenderShowcase(float width, float height);
    Action RenderChoices(float width, float height, bool hasExistingData);

    double clock = 0.0;   // advances with frame time; paused while hovering a slide
};
