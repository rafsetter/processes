#pragma once

#include <windows.h>

#include <string_view>
#include <vector>

namespace Color
{
constexpr WORD GRAY = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
constexpr WORD WHITE = GRAY | FOREGROUND_INTENSITY;
constexpr WORD DARK_GRAY = FOREGROUND_INTENSITY;
constexpr WORD YELLOW = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
constexpr WORD RED = FOREGROUND_RED | FOREGROUND_INTENSITY;
constexpr WORD BLACK_ON_GREEN = BACKGROUND_GREEN | BACKGROUND_INTENSITY;
constexpr WORD WHITE_ON_RED = WHITE | BACKGROUND_RED | BACKGROUND_INTENSITY;
constexpr WORD BLACK_ON_GRAY = BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE;

// The console palette has no orange: these use the dark yellow slot, which Console repaints orange.
constexpr int ORANGE_PALETTE_INDEX = 6;
constexpr COLORREF ORANGE_RGB = RGB(255, 140, 0);
constexpr WORD ORANGE = FOREGROUND_RED | FOREGROUND_GREEN;
constexpr WORD BLACK_ON_ORANGE = BACKGROUND_RED | BACKGROUND_GREEN;
}

// A screen-sized grid of characters and colors. Drawing outside of it is clipped.
class Canvas
{
public:
    Canvas(int width, int height);

    int Width() const;
    int Height() const;
    const CHAR_INFO* Cells() const;

    void Put(int x, int y, wchar_t character, WORD color);
    void Fill(int x, int y, int width, int height, wchar_t character, WORD color);

    // Returns the column right after the text.
    int Text(int x, int y, std::wstring_view text, WORD color);

private:
    int m_width;
    int m_height;
    std::vector<CHAR_INFO> m_cells;
};
