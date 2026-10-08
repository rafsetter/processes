#include "Console.h"

#include <optional>

using namespace std;

namespace
{
HANDLE OpenConsoleDevice(const wchar_t* name)
{
    return CreateFileW(name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
}

// Returns the color it replaced, or nullopt if the console doesn't allow changing it.
optional<COLORREF> SetPaletteColor(HANDLE screen, int index, COLORREF color)
{
    CONSOLE_SCREEN_BUFFER_INFOEX info {};
    info.cbSize = sizeof(info);
    if (!GetConsoleScreenBufferInfoEx(screen, &info))
    {
        return nullopt;
    }

    const COLORREF previous = info.ColorTable[index];
    info.ColorTable[index] = color;

    // Get reports the window's right and bottom edges as inclusive, Set takes them as exclusive:
    // without this the window would shrink by a row and a column.
    ++info.srWindow.Right;
    ++info.srWindow.Bottom;
    if (!SetConsoleScreenBufferInfoEx(screen, &info))
    {
        return nullopt;
    }
    return previous;
}
}

Console::~Console()
{
    Close();
}

bool Console::Open()
{
    // The console's own devices, so it works even if stdin or stdout are redirected.
    m_input.Reset(OpenConsoleDevice(L"CONIN$"));
    m_originalScreen.Reset(OpenConsoleDevice(L"CONOUT$"));
    if (!m_input || !m_originalScreen || !GetConsoleMode(m_input.Get(), &m_originalInputMode))
    {
        return false;
    }

    m_screen.Reset(CreateConsoleScreenBuffer(
        GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CONSOLE_TEXTMODE_BUFFER, nullptr));
    if (!m_screen || !SetConsoleActiveScreenBuffer(m_screen.Get()))
    {
        return false;
    }
    m_open = true;

    // Keys arrive one by one (no line editing, no echo, Ctrl+C is just a key), mouse selection
    // (quick edit) is off, and window resizes are reported.
    SetConsoleMode(m_input.Get(), ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS);

    CONSOLE_CURSOR_INFO cursor { 1, FALSE };
    SetConsoleCursorInfo(m_screen.Get(), &cursor);
    SetConsoleTitleW(L"manager");

    // The palette belongs to the whole console, so Close() puts the original color back.
    m_replacedPaletteColor = SetPaletteColor(m_screen.Get(), Color::ORANGE_PALETTE_INDEX, Color::ORANGE_RGB);

    FitBufferToWindow();
    return true;
}

void Console::Close()
{
    if (!m_open)
    {
        return;
    }

    SetConsoleMode(m_input.Get(), m_originalInputMode);
    SetConsoleActiveScreenBuffer(m_originalScreen.Get());
    if (m_replacedPaletteColor)
    {
        SetPaletteColor(m_originalScreen.Get(), Color::ORANGE_PALETTE_INDEX, *m_replacedPaletteColor);
        m_replacedPaletteColor.reset();
    }
    m_screen.Reset();
    m_open = false;
}

HANDLE Console::Input() const
{
    return m_input.Get();
}

COORD Console::Size() const
{
    CONSOLE_SCREEN_BUFFER_INFO info {};
    if (!GetConsoleScreenBufferInfo(m_screen.Get(), &info))
    {
        return { 80, 25 };
    }
    return {
        static_cast<SHORT>(info.srWindow.Right - info.srWindow.Left + 1),
        static_cast<SHORT>(info.srWindow.Bottom - info.srWindow.Top + 1),
    };
}

void Console::FitBufferToWindow()
{
    const COORD size = Size();
    const SMALL_RECT window { 0, 0, static_cast<SHORT>(size.X - 1), static_cast<SHORT>(size.Y - 1) };
    SetConsoleWindowInfo(m_screen.Get(), TRUE, &window);
    SetConsoleScreenBufferSize(m_screen.Get(), size);
}

void Console::Present(const Canvas& canvas)
{
    CONSOLE_SCREEN_BUFFER_INFO info {};
    GetConsoleScreenBufferInfo(m_screen.Get(), &info);

    SMALL_RECT region {
        info.srWindow.Left,
        info.srWindow.Top,
        static_cast<SHORT>(info.srWindow.Left + canvas.Width() - 1),
        static_cast<SHORT>(info.srWindow.Top + canvas.Height() - 1),
    };
    const COORD size { static_cast<SHORT>(canvas.Width()), static_cast<SHORT>(canvas.Height()) };
    WriteConsoleOutputW(m_screen.Get(), canvas.Cells(), size, { 0, 0 }, &region);
}
