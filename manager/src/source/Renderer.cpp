#include "Renderer.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace std;

namespace
{
constexpr wchar_t HORIZONTAL = L'\u2500';   // ─
constexpr wchar_t VERTICAL = L'\u2502';     // │
constexpr wchar_t TOP_LEFT = L'\u250C';     // ┌
constexpr wchar_t TOP_RIGHT = L'\u2510';    // ┐
constexpr wchar_t BOTTOM_LEFT = L'\u2514';  // └
constexpr wchar_t BOTTOM_RIGHT = L'\u2518'; // ┘

// Straight down when the child is right below the start point; otherwise a diagonal made of
// slashes, joined by underscores when it has to cover more columns than rows.
void DrawConnector(Canvas& canvas, const Connector& connector, int scrollX, int scrollY, WORD color)
{
    const int startX = connector.startX - scrollX;
    const int endX = connector.endX - scrollX;
    const int startY = connector.startY - scrollY;
    const int rows = connector.endY - connector.startY + 1;
    const bool towardsLeft = endX < startX;

    // Stop one column short, so the slash points at the child's center.
    const int lastX = towardsLeft ? endX + 1 : endX - 1;
    if (rows < 2 || (towardsLeft ? lastX >= startX : lastX <= startX))
    {
        for (int row = 0; row < rows; ++row)
        {
            canvas.Put(endX, startY + row, VERTICAL, color);
        }
        return;
    }

    vector<int> slashX(rows);
    for (int row = 0; row < rows; ++row)
    {
        slashX[row] = startX + static_cast<int>(lround(static_cast<double>(lastX - startX) * row / (rows - 1)));
    }

    for (int row = 0; row < rows; ++row)
    {
        canvas.Put(slashX[row], startY + row, towardsLeft ? L'/' : L'\\', color);
        if (row + 1 < rows)
        {
            const int from = min(slashX[row], slashX[row + 1]) + 1;
            const int to = max(slashX[row], slashX[row + 1]) - 1;
            for (int x = from; x <= to; ++x)
            {
                canvas.Put(x, startY + row, L'_', color);
            }
        }
    }
}

void DrawBox(Canvas& canvas, int x, int y, const NodeBox& box, const vector<wstring>& lines, WORD color)
{
    const int right = x + box.width - 1;
    const int bottom = y + box.height - 1;

    canvas.Fill(x, y, box.width, box.height, L' ', color);
    for (int column = x + 1; column < right; ++column)
    {
        canvas.Put(column, y, HORIZONTAL, color);
        canvas.Put(column, bottom, HORIZONTAL, color);
    }
    for (int row = y + 1; row < bottom; ++row)
    {
        canvas.Put(x, row, VERTICAL, color);
        canvas.Put(right, row, VERTICAL, color);
    }
    canvas.Put(x, y, TOP_LEFT, color);
    canvas.Put(right, y, TOP_RIGHT, color);
    canvas.Put(x, bottom, BOTTOM_LEFT, color);
    canvas.Put(right, bottom, BOTTOM_RIGHT, color);

    for (size_t i = 0; i < lines.size(); ++i)
    {
        canvas.Text(x + (box.width - static_cast<int>(lines[i].size())) / 2, y + 1 + static_cast<int>(i), lines[i], color);
    }
}

WORD BoxColor(const ProcessNode& node, bool selected)
{
    if (node.diedAt)
    {
        return Color::WHITE_ON_RED;
    }
    if (selected)
    {
        return Color::BLACK_ON_GREEN;
    }
    return node.killTree ? Color::ORANGE : Color::WHITE;
}

void DrawOrphansLabel(Canvas& canvas, int y)
{
    constexpr wstring_view label = L" orphans ";
    canvas.Fill(0, y, canvas.Width(), 1, HORIZONTAL, Color::DARK_GRAY);
    canvas.Text((canvas.Width() - static_cast<int>(label.size())) / 2, y, label, Color::YELLOW);
}

void DrawStatus(Canvas& canvas, wstring_view status, DWORD selectedPid)
{
    const int y = canvas.Height() - FOOTER_ROWS;
    canvas.Fill(0, y, canvas.Width(), 1, L' ', Color::GRAY);
    canvas.Text(1, y, status, Color::YELLOW);

    if (selectedPid != 0)
    {
        const wstring selected = L"selected " + to_wstring(selectedPid);
        canvas.Text(canvas.Width() - static_cast<int>(selected.size()) - 1, y, selected, Color::GRAY);
    }
}

void DrawRibbon(Canvas& canvas, bool killTreeMode)
{
    struct Command
    {
        wstring_view key;
        wstring_view label;
        WORD labelColor = Color::BLACK_ON_GRAY;
    };
    const Command commands[] = {
        { L"c", L"Create" },
        { L"d", L"Delete" },
        { L"t", killTreeMode ? L"[x] Kill tree" : L"[ ] Kill tree", killTreeMode ? Color::BLACK_ON_ORANGE : Color::BLACK_ON_GRAY },
        { L"\u2190\u2191\u2193\u2192", L"Navigate" }, // ←↑↓→
        { L"q", L"Quit" },
    };

    const int y = canvas.Height() - 1;
    canvas.Fill(0, y, canvas.Width(), 1, L' ', Color::BLACK_ON_GRAY);

    int x = 1;
    for (const Command& command : commands)
    {
        x = canvas.Text(x, y, L" " + wstring(command.key) + L" ", Color::BLACK_ON_GREEN);
        x = canvas.Text(x, y, L" " + wstring(command.label) + L" ", command.labelColor);
        x = canvas.Text(x, y, L"  ", Color::BLACK_ON_GRAY);
    }
}
}

void DrawScreen(
    Canvas& canvas,
    const ProcessModel& model,
    const Layout& layout,
    DWORD selectedPid,
    int scrollX,
    int scrollY,
    std::wstring_view status,
    bool killTreeMode)
{
    // A process killed outside of the program is shown in red, along with the lines to it and from it.
    for (const Connector& connector : layout.connectors)
    {
        const bool dying = model.IsDying(connector.parentPid) || model.IsDying(connector.childPid);
        DrawConnector(canvas, connector, scrollX, scrollY, dying ? Color::RED : Color::GRAY);
    }

    if (layout.orphansLabelY)
    {
        DrawOrphansLabel(canvas, *layout.orphansLabelY - scrollY);
    }

    for (const auto& [pid, box] : layout.boxes)
    {
        vector<wstring> lines;
        if (pid == model.RootPid())
        {
            lines.push_back(L"ROOT");
        }
        lines.push_back(to_wstring(pid));

        DrawBox(canvas, box.x - scrollX, box.y - scrollY, box, lines, BoxColor(*model.Find(pid), pid == selectedPid));
    }

    // Drawn last, over anything of the trees that reaches the bottom rows.
    DrawStatus(canvas, status, selectedPid);
    DrawRibbon(canvas, killTreeMode);
}
