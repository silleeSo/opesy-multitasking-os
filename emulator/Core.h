#pragma once
#include <memory>       
#include <atomic>      
#include <thread>       
#include <functional>
#include <chrono> 
#include "Process.h"
#include "GlobalState.h"

class Scheduler;

class Core {
public:
    Core(int id, Scheduler* scheduler, uint64_t delayPerExec);
    ~Core();

    int id_;
    bool isBusy() const;

    bool tryAssign(std::shared_ptr<Process> p, uint64_t quantum);

    std::shared_ptr<Process> getRunningProcess() const {
        return busy_ ? runningProcess : nullptr;
    }

    void stop();

private:
    void workerLoop(std::shared_ptr<Process> p, uint64_t quantum);
    std::atomic<bool> busy_;
    std::thread worker_;
    std::shared_ptr<Process> runningProcess;

    Scheduler* scheduler;
    uint64_t delayPerExec_;
};