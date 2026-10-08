#include "Watcher.h"

using namespace std;

Watcher::Watcher(chrono::milliseconds interval, function<void()> check)
    : m_thread([this, interval, check = move(check)](stop_token stop)
    {
        unique_lock lock(m_mutex);
        while (true)
        {
            // Returns early when a stop is requested.
            m_wakeUp.wait_for(lock, stop, interval, [] { return false; });
            if (stop.stop_requested())
            {
                return;
            }

            lock.unlock();
            check();
            lock.lock();
        }
    })
{
}
