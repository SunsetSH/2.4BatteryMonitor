#pragma once
// Settings window: sections of rows (title, explanation, current value) drawn in the app's palette.
// A row is activated by a click, Enter or Space: a switch flips, a choice opens a menu at the row.
// The rows come from a builder (settings_rows.cpp) and are rebuilt after every change.
#include <windows.h>

#include <functional>
#include <string>
#include <vector>

namespace settings_window {

struct Row {
    std::wstring section;  // non-empty: a section heading drawn above this row
    std::wstring title;
    std::wstring detail;   // one line under the title, muted
    std::wstring value;    // right-aligned: "On", "Off", "60 s", ...
    bool enabled = true;
    // Runs on activation; `anchor` is the row's bottom-left corner on the screen, for a menu.
    std::function<void(HWND window, POINT anchor)> activate;
};

using Builder = std::function<std::vector<Row>()>;

void Open(HINSTANCE instance, Builder builder);
void Refresh();  // rebuilds the rows, e.g. after a setting changed elsewhere
HWND Window();
void Destroy();

}  // namespace settings_window
