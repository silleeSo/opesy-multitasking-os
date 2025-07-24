#pragma once
#include <vector>
#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <unordered_set>
#include <queue>  

#include "Core.h"
#include "Process.h"
#include "ThreadedQueue.h"
#include "GlobalState.h"
#include "MemoryManager.h" 

class Scheduler {
public:
    Scheduler(int num_cpu, const std::string& scheduler_type, uint64_t quantum_cycles,
        uint64_t batch_process_freq, uint64_t min_ins, uint64_t max_ins,
        uint64_t delay_per_exec, MemoryManager& memoryManager);

    ~Scheduler();

    void start();
    void stop();
    void submit(std::shared_ptr<Process> p);
    void requeueProcess(std::shared_ptr<Process> p);
    void startProcessGeneration();
    void stopProcessGeneration();
    void waitUntilAllDone();

    void addFinishedProcess(std::shared_ptr<Process> p);
    uint64_t getNextProcessId();

    std::vector<std::shared_ptr<Process>> getRunningProcesses() const;
    std::vector<std::shared_ptr<Process>> getFinishedProcesses() const;
    std::vector<std::shared_ptr<Process>> getSleepingProcesses() const;

    double getCpuUtilization() const;
    size_t getCoresUsed() const;
    size_t getCoresAvailable() const;


    void updateCoreUtilization(int coreId, uint64_t ticksUsed);
    Core* getCore(int index) const;

private:
    void schedulerLoop();
    void processGeneratorLoop();

    // Core specs
    int numCpus_;
    size_t nextCoreIndex_ = 0; 
    std::string schedulerType_;
    uint64_t quantumCycles_;
    uint64_t batchProcessFreq_;
    uint64_t minInstructions_;
    uint64_t maxInstructions_;
    uint64_t delayPerExec_;

    // Core and process queues
    std::vector<std::unique_ptr<Core>> cores_;
    TSQueue<std::shared_ptr<Process>> readyQueue_;
    std::queue<std::shared_ptr<Process>> memoryPendingQueue; 

    // Sleeping processes
    mutable std::mutex sleepingProcessesMutex_;
    std::vector<std::shared_ptr<Process>> sleepingProcesses_;

    // Finished processes
    mutable std::mutex finishedProcessesMutex_;
    std::vector<std::shared_ptr<Process>> finishedProcesses_;
    std::unordered_set<uint64_t> finishedPIDs_;

    // Running control
    std::thread schedulerThread_;
    std::atomic<bool> running_ = false;

    std::thread processGenThread_;
    std::atomic<bool> processGenEnabled_ = false;
    std::atomic<uint64_t> lastProcessGenTick_ = 0;

    std::atomic<uint64_t> nextPid_ = 1;
    std::atomic<int> activeProcessesCount_ = 0;

    std::vector<std::unique_ptr<std::atomic<uint64_t>>> coreTicksUsed_;
    std::atomic<uint64_t> schedulerStartTime_ = 0;

    // Memory Management Support
    MemoryManager& memoryManager_;
    uint64_t lastQuantumSnapshot_ = 0;
    uint64_t quantumIndex_ = 0;
};
