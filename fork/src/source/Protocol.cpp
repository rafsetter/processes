#include "Protocol.h"

#include <charconv>

using namespace std;

namespace Protocol
{
string WithPid(string_view word, DWORD pid)
{
    return string(word) + " " + to_string(pid);
}

string Failed(string_view reason)
{
    return string(REPLY_FAILED) + " " + string(reason);
}

optional<DWORD> ParsePid(string_view text)
{
    DWORD pid = 0;
    const auto [end, error] = from_chars(text.data(), text.data() + text.size(), pid);
    if (error != errc {} || end != text.data() + text.size() || pid == 0)
    {
        return nullopt;
    }
    return pid;
}

optional<DWORD> ParseWithPid(string_view message, string_view word)
{
    if (!message.starts_with(word) || message.size() <= word.size() || message[word.size()] != ' ')
    {
        return nullopt;
    }
    return ParsePid(message.substr(word.size() + 1));
}
}
