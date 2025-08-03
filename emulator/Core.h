#pragma once
#include <memory>       // Required for std::shared_ptr
#include <atomic>       // Required for std::atomic
#include <thread>       // Required for std::thread
#include <functional>
#include <chrono> 
#include "Process.h"
#include "GlobalState.h"

// Forward declare Scheduler to avoid circular include
class Scheduler;

class Core {
public:
    Core(int id, Scheduler* scheduler, uint64_t delayPerExec);
    ~Core();

    int id_;
    bool isBusy() const;

    // --- FIX: Added std:: prefix ---
    bool tryAssign(std::shared_ptr<Process> p, uint64_t quantum);

    // --- FIX: Added std:: prefix ---
    std::shared_ptr<Process> getRunningProcess() const {
        return busy_ ? runningProcess : nullptr;
    }

    void stop();

private:
    // --- FIX: Added std:: prefix ---
    void workerLoop(std::shared_ptr<Process> p, uint64_t quantum);

    // --- FIX: Added std:: prefix ---
    std::atomic<bool> busy_;
    std::thread worker_;
    std::shared_ptr<Process> runningProcess;

    Scheduler* scheduler;
    uint64_t delayPerExec_;
};