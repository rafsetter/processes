#pragma once

#include <windows.h>

#include <optional>

#include "Canvas.h"
#include "UniqueHandle.h"

// Full-screen console mode: a separate screen buffer (like a terminal's alternate screen) and
// raw keyboard input. Close() (or the destructor) puts the console back as it was.
class Console
{
public:
    ~Console();

    bool Open();
    void Close();

    // Signaled when there is input to read.
    HANDLE Input() const;

    // Visible size, in characters.
    COORD Size() const;

    // Makes the buffer exactly as big as the window, so there are no scroll bars. Call after a resize.
    void FitBufferToWindow();

    void Present(const Canvas& canvas);

private:
    UniqueHandle m_input;
    UniqueHandle m_originalScreen;
    UniqueHandle m_screen;
    DWORD m_originalInputMode = 0;
    std::optional<COLORREF> m_replacedPaletteColor;
    bool m_open = false;
};
