#pragma once

#include <windows.h>

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Ddc.h"
#include "PendingWrites.h"

struct MonitorState
{
    std::wstring name;
    int brightness = 0;   // percent
    bool hasContrast = false;
    int contrast = 0;     // percent
};

// What the UI shows. The generation changes whenever the monitors are enumerated again; Set() calls
// carrying an older generation are dropped, because their monitor indices may no longer match.
struct DdcSnapshot
{
    uint64_t generation = 0;   // 0 = not enumerated yet
    std::vector<MonitorState> monitors;
};

// Owns the physical monitor handles and does all DDC/CI I/O on one background thread.
class DdcWorker
{
public:
    using SnapshotCallback = std::function<void(DdcSnapshot)>;   // called on the worker thread

    explicit DdcWorker(SnapshotCallback onSnapshot);
    ~DdcWorker();
    DdcWorker(const DdcWorker&) = delete;
    DdcWorker& operator=(const DdcWorker&) = delete;

    void Start();   // enumerates right away
    void Stop();

    void RequestEnumerate();
    void RequestRead();
    void Set(uint64_t generation, size_t monitor, BYTE code, int percent);

private:
    void Run();
    void Publish() const;

    SnapshotCallback onSnapshot_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_ = false;
    bool enumerate_ = true;
    bool read_ = false;
    uint64_t generation_ = 0;   // written by the worker thread under mutex_
    PendingWrites pending_;
    std::thread thread_;

    std::vector<DdcMonitor> monitors_;   // worker thread only
};
