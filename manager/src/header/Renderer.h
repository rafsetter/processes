#pragma once

#include <windows.h>

#include <string_view>

#include "Canvas.h"
#include "Layout.h"
#include "ProcessModel.h"

// Rows at the bottom of the screen used by the status line and the command ribbon.
constexpr int FOOTER_ROWS = 2;

// Draws the trees (scrolled by scrollX/scrollY), the status line and the command ribbon.
// Processes that kill their tree are orange; killTreeMode is the ribbon's checkbox.
void DrawScreen(
    Canvas& canvas,
    const ProcessModel& model,
    const Layout& layout,
    DWORD selectedPid,
    int scrollX,
    int scrollY,
    std::wstring_view status,
    bool killTreeMode);
