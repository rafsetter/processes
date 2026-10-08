#pragma once

#include <windows.h>

#include <optional>
#include <unordered_map>
#include <vector>

#include "ProcessModel.h"

// Positions are in "drawing" coordinates: the whole drawing can be larger than the screen.
struct NodeBox
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    int CenterX() const { return x + width / 2; }
};

// A line from a parent's bottom edge to a child's top edge, over the rows startY..endY.
struct Connector
{
    DWORD parentPid = 0;
    DWORD childPid = 0;
    int startX = 0;
    int startY = 0;
    int endX = 0;
    int endY = 0;
};

struct Layout
{
    std::unordered_map<DWORD, NodeBox> boxes;
    std::vector<Connector> connectors;
    std::optional<int> orphansLabelY; // Row of the "orphans" separator, when there are orphans
    int width = 0;
    int height = 0;
};

// Draws the trees top to bottom: the root's tree centered at the top, the orphan trees below it.
Layout ComputeLayout(const ProcessModel& model, int screenWidth);

enum class Direction
{
    Up,
    Down,
    Left,
    Right,
};

// The node to select when moving from pid in the given direction, if there is one.
std::optional<DWORD> FindNeighbor(const Layout& layout, const ProcessModel& model, DWORD pid, Direction direction);
