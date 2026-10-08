#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

// Calls a function periodically on its own thread, until it's destroyed.
class Watcher
{
public:
    Watcher(std::chrono::milliseconds interval, std::function<void()> check);

    Watcher(const Watcher&) = delete;
    Watcher& operator=(const Watcher&) = delete;

private:
    std::mutex m_mutex;
    std::condition_variable_any m_wakeUp;
    std::jthread m_thread; // Last, so it stops (and joins) before the rest is destroyed
};
