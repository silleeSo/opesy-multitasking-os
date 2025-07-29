// Core.h
/*
* CORE OVERVIEW
    - Tracks whether it's busy or free
    - Can be assigned a process by Scheduler
    - Runs process instructions by calling p->runQuantum(quantum)
    - Works for both RR and FCFS (based on quantum value)
*/
#pragma once
#include <memory>
#include <atomic>
#include <thread>
#include <functional>
#include <chrono> // For sleep_for
#include "Process.h"
#include "GlobalState.h" // Include for globalCpuTicks
using namespace std;

// Forward declare Scheduler to avoid circular include
class Scheduler;

class Core {
public:
    Core(int id, Scheduler* scheduler, uint64_t delayPerExec);  // inject Scheduler reference and delay
    ~Core();

    int id_;
    bool isBusy() const;

    // Called by Scheduler to assign a Process
    bool tryAssign(shared_ptr<Process> p, uint64_t quantum);

    // Get the currently running process (for screen -ls/report-util)
    shared_ptr<Process> getRunningProcess() const {
        // Return a copy of the shared_ptr if busy, nullptr otherwise
        return busy_ ? runningProcess : nullptr;
    }

    void stop();


private:
    void workerLoop(shared_ptr<Process> p, uint64_t quantum);

    atomic<bool> busy_;
    thread worker_;
    shared_ptr<Process> runningProcess; // The process currently assigned to this core

    Scheduler* scheduler;  // to notify Scheduler if quantum expires or process finishes/sleeps
    uint64_t delayPerExec_; // Delay in CPU ticks per instruction execution
};