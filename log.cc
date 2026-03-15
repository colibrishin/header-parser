#include "log.h"
#include <iostream>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>

namespace {

enum StreamId : int { StdOut = 0, StdErr = 1 };

std::queue<std::pair<int, std::string>> g_logQueue;
std::mutex g_logMutex;
std::condition_variable g_logCv;
bool g_logShutdown = false;
std::thread g_logThread;

void logThreadFunc()
{
    while (true)
    {
        std::unique_lock<std::mutex> lock(g_logMutex);
        g_logCv.wait(lock, []
        {
            return (g_logShutdown && g_logQueue.empty()) || !g_logQueue.empty();
        });
        if (g_logQueue.empty() && g_logShutdown)
            break;
        if (g_logQueue.empty())
            continue;
        std::pair<int, std::string> item = std::move(g_logQueue.front());
        g_logQueue.pop();
        lock.unlock();

        if (item.first == StdOut)
            std::cout << item.second << std::flush;
        else
            std::cerr << item.second << std::flush;
    }
}

} // namespace

void StartLogThread()
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logThread.joinable())
        return;
    g_logShutdown = false;
    g_logThread = std::thread(logThreadFunc);
}

void StopLogThread()
{
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        g_logShutdown = true;
    }
    g_logCv.notify_all();
    if (g_logThread.joinable())
        g_logThread.join();
}

void LogOut(const std::string& s)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (!g_logThread.joinable())
        return;
    g_logQueue.emplace(StdOut, s);
    g_logCv.notify_one();
}

void LogErr(const std::string& s)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (!g_logThread.joinable())
        return;
    g_logQueue.emplace(StdErr, s);
    g_logCv.notify_one();
}
