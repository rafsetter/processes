#pragma once

#include <windows.h>

#include <utility>

// Owns a Win32 HANDLE and closes it when it goes out of scope.
class UniqueHandle
{
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) : m_handle(Normalize(handle)) {}
    ~UniqueHandle() { Reset(); }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : m_handle(std::exchange(other.m_handle, nullptr)) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept
    {
        if (this != &other)
        {
            Reset(std::exchange(other.m_handle, nullptr));
        }
        return *this;
    }

    void Reset(HANDLE handle = nullptr)
    {
        if (m_handle)
        {
            CloseHandle(m_handle);
        }
        m_handle = Normalize(handle);
    }

    HANDLE Get() const { return m_handle; }
    explicit operator bool() const { return m_handle != nullptr; }

private:
    // Some APIs return INVALID_HANDLE_VALUE on failure, others nullptr.
    static HANDLE Normalize(HANDLE handle) { return handle == INVALID_HANDLE_VALUE ? nullptr : handle; }

    HANDLE m_handle = nullptr;
};
