#include "Scheduler.h"
#include "Core.h"
#include <algorithm>
#include <chrono>
#include <random>
#include <iostream>
#include <thread>
#include <mutex>

static std::random_device scheduler_rd;
static std::mt19937 scheduler_gen(scheduler_rd());

Scheduler::Scheduler(int num_cpu, const std::string& scheduler_type, uint64_t quantum_cycles,
    uint64_t batch_process_freq, uint64_t min_ins, uint64_t max_ins, uint64_t delay_per_exec,
    MemoryManager& memoryManager, int frameSize)
    : numCpus_(num_cpu), schedulerType_(scheduler_type), quantumCycles_(quantum_cycles),
    batchProcessFreq_(batch_process_freq), minInstructions_(min_ins), maxInstructions_(max_ins),
    delayPerExec_(delay_per_exec), running_(false), processGenEnabled_(false),
    lastProcessGenTick_(0), nextPid_(1), activeProcessesCount_(0),
    schedulerStartTime_(0), memoryManager_(memoryManager), frameSize_(frameSize),
    lastQuantumSnapshot_(0), quantumIndex_(0) {

    cores_.reserve(numCpus_);
    for (int i = 0; i < numCpus_; ++i) {
        cores_.emplace_back(std::make_unique<Core>(i, this, delayPerExec_));
        coreTicksUsed_.emplace_back(std::make_unique<std::atomic<uint64_t>>(0));
    }
}

Scheduler::~Scheduler() {
    stop();
}

void Scheduler::start() {
    if (!running_.load()) {
        running_ = true;
        schedulerStartTime_ = globalCpuTicks.load();
        schedulerThread_ = std::thread(&Scheduler::schedulerLoop, this);
    }
}

void Scheduler::stop() {
    for (const auto& core : cores_) core->stop();
    running_ = false;
    processGenEnabled_ = false;
    if (schedulerThread_.joinable()) schedulerThread_.join();
    if (processGenThread_.joinable()) processGenThread_.join();
}

void Scheduler::submit(std::shared_ptr<Process> p) {
    readyQueue_.push(p);
    activeProcessesCount_++;
}

void Scheduler::requeueProcess(std::shared_ptr<Process> p) {
    if (p->isSleeping()) {
        std::lock_guard<std::mutex> lock(sleepingProcessesMutex_);
        sleepingProcesses_.push_back(p);
    }
    else {
        readyQueue_.push(p);
    }
}

void Scheduler::addFinishedProcess(std::shared_ptr<Process> p) {
    std::lock_guard<std::mutex> lock(finishedProcessesMutex_);
    if (finishedPIDs_.insert(p->getPid()).second) {
        p->setFinishTime(time(nullptr));
        memoryManager_.deallocate(p->getPid());
        finishedProcesses_.push_back(p);
        activeProcessesCount_--;
    }
}

void Scheduler::startProcessGeneration() {
    if (!processGenEnabled_.load()) {
        processGenEnabled_ = true;
        lastProcessGenTick_ = globalCpuTicks.load();
        processGenThread_ = std::thread(&Scheduler::processGeneratorLoop, this);
    }
}

void Scheduler::stopProcessGeneration() {
    processGenEnabled_ = false;
    if (processGenThread_.joinable()) processGenThread_.join();
}

void Scheduler::waitUntilAllDone() {
    while (activeProcessesCount_.load() > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

uint64_t Scheduler::getNextProcessId() {
    return nextPid_++;
}

std::vector<std::shared_ptr<Process>> Scheduler::getRunningProcesses() const {
    std::vector<std::shared_ptr<Process>> running;
    for (const auto& core : cores_) {
        if (core->isBusy()) running.push_back(core->getRunningProcess());
    }
    return running;
}

std::vector<std::shared_ptr<Process>> Scheduler::getFinishedProcesses() const {
    std::vector<std::shared_ptr<Process>> temp_copy;
    {
        std::lock_guard<std::mutex> lock(finishedProcessesMutex_);
        temp_copy = finishedProcesses_; 
    } 
    return temp_copy; 
}

std::vector<std::shared_ptr<Process>> Scheduler::getSleepingProcesses() const {
    std::lock_guard<std::mutex> lock(sleepingProcessesMutex_);
    return sleepingProcesses_;
}

double Scheduler::getCpuUtilization() const {
    if (numCpus_ == 0) return 0.0;
    return static_cast<double>(getCoresUsed()) / numCpus_ * 100.0;
}

size_t Scheduler::getCoresUsed() const {
    return std::count_if(cores_.begin(), cores_.end(), [](const auto& core) {
        return core->isBusy();
        });
}


size_t Scheduler::getCoresAvailable() const {
    return numCpus_ - getCoresUsed();
}


uint64_t Scheduler::getActiveCpuTicks() const {
    uint64_t totalActiveTicks = 0;
    for (const auto& coreTicks : coreTicksUsed_) {
        totalActiveTicks += coreTicks->load();
    }
    return totalActiveTicks;
}


void Scheduler::updateCoreUtilization(int coreId, uint64_t ticksUsed) {
    if (coreId >= 0 && coreId < numCpus_) {
        coreTicksUsed_[coreId]->fetch_add(ticksUsed);
    }
}

Core* Scheduler::getCore(int index) const {
    if (index >= 0 && index < static_cast<int>(cores_.size())) {
        return cores_[index].get();
    }
    return nullptr;
}

void Scheduler::schedulerLoop() {
    while (running_.load()) {
        {
            std::lock_guard<std::mutex> lock(sleepingProcessesMutex_);
            auto now = globalCpuTicks.load();
            auto it = sleepingProcesses_.begin();
            while (it != sleepingProcesses_.end()) {
                if ((*it)->isSleeping() && now >= (*it)->getSleepTargetTick()) {
                    (*it)->setIsSleeping(false);
                    readyQueue_.push(*it);
                    it = sleepingProcesses_.erase(it);
                }
                else {
                    ++it;
                }
            }
        }

        for (auto& core : cores_) {
            if (!core->isBusy()) {
                std::shared_ptr<Process> p;
                if (readyQueue_.try_pop(p)) {

                    // Assign the process directly to the core
                    uint64_t quantum = (schedulerType_ == "rr") ? quantumCycles_ : UINT64_MAX;
                    core->tryAssign(p, quantum);
                }
            }
        }

        for (auto& core : cores_) {
            auto p = core->getRunningProcess();
            if (p && p->isFinished()) {
                addFinishedProcess(p);
            }
        }


        // Log a memory snapshot periodically
        uint64_t now = globalCpuTicks.load();
        if (quantumCycles_ > 0 && (now - lastQuantumSnapshot_) >= quantumCycles_) {
            memoryManager_.logMemorySnapshot();
            lastQuantumSnapshot_ = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void Scheduler::processGeneratorLoop() {
    while (processGenEnabled_.load()) {
        uint64_t now = globalCpuTicks.load();
        if (now >= lastProcessGenTick_ + batchProcessFreq_) {
            uint64_t pid = getNextProcessId();
            std::string name = "p" + std::to_string(pid);

            int memToAlloc = memoryManager_.getRandomMemorySize();
            auto proc = std::make_shared<Process>(pid, name, &memoryManager_);

            proc->setAllocatedMemory(memToAlloc);

            submit(proc);
            lastProcessGenTick_ = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

std::shared_ptr<Process> Scheduler::findProcessById(uint64_t pid) const {
    // Search running processes on cores
    for (const auto& core : cores_) {
        auto p = core->getRunningProcess();
        if (p && p->getPid() == pid) return p;
    }


    // Search sleeping processes
    {
        std::lock_guard<std::mutex> sleep_lock(sleepingProcessesMutex_);
        for (const auto& p : sleepingProcesses_) {
            if (p->getPid() == pid) return p;
        }
    }

    // Search finished processes
    {
        std::lock_guard<std::mutex> finish_lock(finishedProcessesMutex_);
        for (const auto& p : finishedProcesses_) {
            if (p->getPid() == pid) return p;
        }
    }

    return nullptr;
}