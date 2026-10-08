#include "Layout.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <string>

using namespace std;

namespace
{
constexpr int MIN_BOX_WIDTH = 10;
constexpr int BOX_HEIGHT = 3;      // Border, pid, border
constexpr int ROOT_BOX_HEIGHT = 4; // Border, ROOT, pid, border
constexpr int CONNECTOR_ROWS = 3;
constexpr int SIBLING_GAP = 2;
constexpr int ORPHAN_TREE_GAP = 4;

class LayoutBuilder
{
public:
    LayoutBuilder(const ProcessModel& model, Layout& layout, int boxWidth)
        : m_model(model), m_layout(layout), m_boxWidth(boxWidth)
    {
    }

    // Width needed by the subtree of pid.
    int Measure(DWORD pid)
    {
        const vector<DWORD>& children = m_model.Find(pid)->children;
        int width = 0;
        for (const DWORD child : children)
        {
            width += Measure(child);
        }
        width += SIBLING_GAP * max(0, static_cast<int>(children.size()) - 1);
        width = max(width, m_boxWidth);

        m_widths[pid] = width;
        return width;
    }

    // Places the (measured) subtree of pid in the columns [left, left + width), with pid's box at row y.
    void Place(DWORD pid, int left, int y)
    {
        const vector<DWORD>& children = m_model.Find(pid)->children;
        const int width = m_widths.at(pid);
        const int height = pid == m_model.RootPid() ? ROOT_BOX_HEIGHT : BOX_HEIGHT;
        int centerX = left + width / 2;

        if (!children.empty())
        {
            int childrenWidth = SIBLING_GAP * (static_cast<int>(children.size()) - 1);
            for (const DWORD child : children)
            {
                childrenWidth += m_widths.at(child);
            }

            int childLeft = left + (width - childrenWidth) / 2;
            for (const DWORD child : children)
            {
                Place(child, childLeft, y + height + CONNECTOR_ROWS);
                childLeft += m_widths.at(child) + SIBLING_GAP;
            }

            centerX = (m_layout.boxes.at(children.front()).CenterX() + m_layout.boxes.at(children.back()).CenterX()) / 2;
        }

        const NodeBox box { centerX - m_boxWidth / 2, y, m_boxWidth, height };
        m_layout.boxes[pid] = box;

        const int count = static_cast<int>(children.size());
        const int halfSpread = (box.width - 2) / 2 - 1;
        for (int i = 0; i < count; ++i)
        {
            const NodeBox& child = m_layout.boxes.at(children[i]);

            // Lines leave from points spread evenly (and symmetrically) along the bottom edge, so
            // they don't overlap. A single child's line leaves from the center.
            const double offset = count == 1 ? 0.0 : -halfSpread + 2.0 * halfSpread * i / (count - 1);

            Connector connector;
            connector.parentPid = pid;
            connector.childPid = children[i];
            connector.startX = box.CenterX() + static_cast<int>(lround(offset));
            connector.startY = box.y + box.height;
            connector.endX = child.CenterX();
            connector.endY = child.y - 1;
            m_layout.connectors.push_back(connector);
        }
    }

private:
    const ProcessModel& m_model;
    Layout& m_layout;
    const int m_boxWidth;
    unordered_map<DWORD, int> m_widths;
};

void Shift(Layout& layout, int dx)
{
    for (auto& [pid, box] : layout.boxes)
    {
        box.x += dx;
    }
    for (Connector& connector : layout.connectors)
    {
        connector.startX += dx;
        connector.endX += dx;
    }
}

int Bottom(const Layout& layout)
{
    int bottom = 0;
    for (const auto& [pid, box] : layout.boxes)
    {
        bottom = max(bottom, box.y + box.height);
    }
    return bottom;
}
}

Layout ComputeLayout(const ProcessModel& model, int screenWidth)
{
    Layout layout;
    const DWORD rootPid = model.RootPid();
    if (!model.Find(rootPid))
    {
        return layout;
    }

    int boxWidth = MIN_BOX_WIDTH;
    for (const auto& [pid, node] : model.Nodes())
    {
        boxWidth = max(boxWidth, static_cast<int>(to_string(pid).size()) + 4);
    }

    LayoutBuilder builder(model, layout, boxWidth);

    // The root goes at the center of the screen, unless that would push its tree off the screen.
    const int treeWidth = builder.Measure(rootPid);
    builder.Place(rootPid, 0, 0);
    int shift = screenWidth / 2 - layout.boxes.at(rootPid).CenterX();
    if (shift < 0 || shift + treeWidth > screenWidth)
    {
        shift = max(0, (screenWidth - treeWidth) / 2);
    }
    Shift(layout, shift);

    if (!model.Orphans().empty())
    {
        const int labelY = Bottom(layout) + 1;
        layout.orphansLabelY = labelY;

        int orphansWidth = ORPHAN_TREE_GAP * (static_cast<int>(model.Orphans().size()) - 1);
        for (const DWORD orphan : model.Orphans())
        {
            orphansWidth += builder.Measure(orphan);
        }

        int left = max(0, (screenWidth - orphansWidth) / 2);
        for (const DWORD orphan : model.Orphans())
        {
            builder.Place(orphan, left, labelY + 2);
            left += builder.Measure(orphan) + ORPHAN_TREE_GAP;
        }
    }

    layout.width = screenWidth;
    for (const auto& [pid, box] : layout.boxes)
    {
        layout.width = max(layout.width, box.x + box.width);
    }
    layout.height = Bottom(layout);
    return layout;
}

optional<DWORD> FindNeighbor(const Layout& layout, const ProcessModel& model, DWORD pid, Direction direction)
{
    const auto current = layout.boxes.find(pid);
    const ProcessNode* node = model.Find(pid);
    if (current == layout.boxes.end() || !node)
    {
        return nullopt;
    }
    const NodeBox& from = current->second;

    if (direction == Direction::Up && layout.boxes.contains(node->parentPid))
    {
        return node->parentPid;
    }

    // Going down from a parent always enters its own subtree.
    const bool onlyChildren = direction == Direction::Down && !node->children.empty();

    optional<DWORD> best;
    pair<int, int> bestScore { INT_MAX, INT_MAX };
    for (const auto& [candidate, box] : layout.boxes)
    {
        if (onlyChildren && model.Find(candidate)->parentPid != pid)
        {
            continue;
        }

        const int dx = box.CenterX() - from.CenterX();
        const int dy = box.y - from.y;

        // Closest row first, then closest column.
        bool eligible = false;
        pair<int, int> score;
        switch (direction)
        {
        case Direction::Up:
            eligible = dy < 0;
            score = { -dy, abs(dx) };
            break;
        case Direction::Down:
            eligible = dy > 0;
            score = { dy, abs(dx) };
            break;
        case Direction::Left:
            eligible = dy == 0 && dx < 0;
            score = { -dx, 0 };
            break;
        case Direction::Right:
            eligible = dy == 0 && dx > 0;
            score = { dx, 0 };
            break;
        }

        if (eligible && score < bestScore)
        {
            best = candidate;
            bestScore = score;
        }
    }
    return best;
}
