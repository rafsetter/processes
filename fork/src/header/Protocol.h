#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <string_view>

// Words of the text protocol: the user's commands to the root, the root's commands to its
// descendants (through their pipes), and the replies to both.
namespace Protocol
{
inline constexpr std::string_view COMMAND_CREATE = "create";
inline constexpr std::string_view COMMAND_DELETE = "delete";
inline constexpr std::string_view COMMAND_LIST = "list";
inline constexpr std::string_view COMMAND_KYS = "kys";

// Optional last word of "create": what happens to the new process's descendants when it dies.
inline constexpr std::string_view MODE_TREE = "tree";     // They die with it
inline constexpr std::string_view MODE_SINGLE = "single"; // They are left orphaned

inline constexpr std::string_view REPLY_CREATED = "created";
inline constexpr std::string_view REPLY_DELETED = "deleted";
inline constexpr std::string_view REPLY_EXITED = "exited";
inline constexpr std::string_view REPLY_FAILED = "failed";

// "<word> <pid>", e.g. "created 1234"
std::string WithPid(std::string_view word, DWORD pid);
std::string Failed(std::string_view reason);

std::optional<DWORD> ParsePid(std::string_view text);

// Returns the PID of a "<word> <pid>" message.
std::optional<DWORD> ParseWithPid(std::string_view message, std::string_view word);
}
