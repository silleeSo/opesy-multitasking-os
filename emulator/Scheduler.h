
#pragma once
#include <vector>
#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <unordered_set> 

#include "Core.h"
#include "Process.h"
#include "ThreadedQueue.h"
#include "GlobalState.h"

class Scheduler {
public:
    Scheduler(int num_cpu, const std::string& scheduler_type, uint64_t quantum_cycles,
        uint64_t batch_process_freq, uint64_t min_ins, uint64_t max_ins,
        uint64_t delay_per_exec);
    ~Scheduler();

    void start();
    void stop();
    void submit(std::shared_ptr<Process> p);
    void notifyProcessFinished();
    void requeueProcess(std::shared_ptr<Process> p);
    void startProcessGeneration();
    void stopProcessGeneration();
    void waitUntilAllDone();

    void addFinishedProcess(std::shared_ptr<Process> p);

    int getNextProcessId();

    std::vector<std::shared_ptr<Process>> getRunningProcesses() const;
    std::vector<std::shared_ptr<Process>> getFinishedProcesses() const;
    std::vector<std::shared_ptr<Process>> getSleepingProcesses() const;

    double getCpuUtilization() const;
    int getCoresUsed() const;
    int getCoresAvailable() const;


    void updateCoreUtilization(int coreId, uint64_t ticksUsed);
    Core* getCore(int index) const;

private:
    void schedulerLoop();
    void processGeneratorLoop();

    int numCpus_;
    int nextCoreIndex_ = 0;
    std::string schedulerType_;
    uint64_t quantumCycles_;
    uint64_t batchProcessFreq_;
    uint64_t minInstructions_;
    uint64_t maxInstructions_;
    uint64_t delayPerExec_;

    std::vector<std::unique_ptr<Core>> cores_;
    TSQueue<std::shared_ptr<Process>> readyQueue_;

    mutable std::mutex runningProcessesMutex_;
    std::vector<std::shared_ptr<Process>> runningProcesses_;

    mutable std::mutex finishedProcessesMutex_;
    std::vector<std::shared_ptr<Process>> finishedProcesses_;
    std::unordered_set<int> finishedPIDs_;

    mutable std::mutex sleepingProcessesMutex_;
    std::vector<std::shared_ptr<Process>> sleepingProcesses_;

    std::thread schedulerThread_;
    std::atomic<bool> running_ = false;

    std::thread processGenThread_;
    std::atomic<bool> processGenEnabled_ = false;
    std::atomic<uint64_t> lastProcessGenTick_ = 0;

    std::atomic<int> nextPid_ = 1;
    std::atomic<int> activeProcessesCount_ = 0;

    std::vector<std::unique_ptr<std::atomic<uint64_t>>> coreTicksUsed_;
    std::atomic<uint64_t> schedulerStartTime_ = 0;
};
