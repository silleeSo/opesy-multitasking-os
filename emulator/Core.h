#pragma once
#include <thread>
#include <atomic>
#include <memory>
#include <cstdint>

class Process;
class Scheduler;

class Core {
public:
    Core(int id, Scheduler* scheduler, uint64_t delayPerExec);
    ~Core();
    void stop();
    void join(); // Added this line
    bool isBusy() const;
    bool tryAssign(std::shared_ptr<Process> p, uint64_t quantum);
    std::shared_ptr<Process> getRunningProcess() const { return runningProcess; }
    void workerLoop(std::shared_ptr<Process> p, uint64_t quantum);

private:
    int id_;
    std::atomic<bool> busy_;
    std::thread worker_;
    Scheduler* scheduler;
    std::shared_ptr<Process> runningProcess;
    uint64_t delayPerExec_;
};