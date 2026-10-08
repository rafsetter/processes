#include "Canvas.h"

#include <algorithm>

using namespace std;

Canvas::Canvas(int width, int height)
    : m_width(max(width, 0))
    , m_height(max(height, 0))
    , m_cells(static_cast<size_t>(m_width) * m_height)
{
    Fill(0, 0, m_width, m_height, L' ', Color::GRAY);
}

int Canvas::Width() const
{
    return m_width;
}

int Canvas::Height() const
{
    return m_height;
}

const CHAR_INFO* Canvas::Cells() const
{
    return m_cells.data();
}

void Canvas::Put(int x, int y, wchar_t character, WORD color)
{
    if (x < 0 || y < 0 || x >= m_width || y >= m_height)
    {
        return;
    }

    CHAR_INFO& cell = m_cells[static_cast<size_t>(y) * m_width + x];
    cell.Char.UnicodeChar = character;
    cell.Attributes = color;
}

void Canvas::Fill(int x, int y, int width, int height, wchar_t character, WORD color)
{
    for (int row = y; row < y + height; ++row)
    {
        for (int column = x; column < x + width; ++column)
        {
            Put(column, row, character, color);
        }
    }
}

int Canvas::Text(int x, int y, wstring_view text, WORD color)
{
    for (const wchar_t character : text)
    {
        Put(x++, y, character, color);
    }
    return x;
}
