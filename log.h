#pragma once

#include <string>

/// Thread-safe ordered log: enqueue stdout/stderr and drain from a single thread.
/// Call StartLogThread() at program start and StopLogThread() before exit.

void StartLogThread();
void StopLogThread();

void LogOut(const std::string& s);
void LogErr(const std::string& s);

inline void LogOut(const char* s) { LogOut(std::string(s)); }
inline void LogErr(const char* s) { LogErr(std::string(s)); }
