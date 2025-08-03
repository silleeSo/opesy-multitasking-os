#include "Core.h"
#include "Scheduler.h"
#include "GlobalState.h"
#include <iostream>
#include <stdexcept> // For std::runtime_error

using std::cout;
using std::endl;
using std::string;
using std::to_string;
using std::cerr;
using std::exception;
using std::thread;

Core::Core(int id, Scheduler* scheduler, uint64_t delayPerExec)
    : id_(id), busy_(false), scheduler(scheduler), delayPerExec_(delayPerExec) {
}

Core::~Core() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

void Core::stop() {
    busy_ = false;
}

bool Core::isBusy() const {
    return busy_;
}

bool Core::tryAssign(std::shared_ptr<Process> p, uint64_t quantum) {
    if (busy_) return false;

    if (worker_.joinable()) {
        worker_.join();
    }

    runningProcess = p;
    p->setLastCoreId(id_);
    busy_ = true;

    try {
        worker_ = std::thread(&Core::workerLoop, this, p, quantum);
    }
    catch (const std::system_error& e) {
        std::cerr << "[Core-" << id_ << "] Failed to start thread: " << e.what() << std::endl;
        busy_ = false;
        runningProcess = nullptr;
        return false;
    }

    return true;
}

void Core::workerLoop(std::shared_ptr<Process> p, uint64_t quantum) {
    if (!p->hasBeenScheduled()) {
        int memToAlloc = p->getAllocatedMemory();
        if (scheduler->getMemoryManager().allocateMemory(p, memToAlloc)) {
            p->setHasBeenScheduled(true);
            // Only generate random instructions if the process's instruction list is currently empty.
            // This prevents overwriting custom instructions from 'screen -c'.
            if (p->getTotalInstructions() == 0) { // Check if no instructions were pre-loaded
                p->genRandInst(scheduler->getMinIns(), scheduler->getMaxIns(), memToAlloc);
            }
        }
        else {
            // If memory allocation fails, the process cannot run.
            // Requeue it and terminate this worker thread.
            if (scheduler) scheduler->requeueProcess(p);
            busy_ = false;
            runningProcess = nullptr;
            return;
        }
    }

    uint64_t executed = 0;

    while (busy_.load() && !p->isFinished() && executed < quantum) {
        if (p->isSleeping()) {
            if (scheduler) scheduler->requeueProcess(p);
            break;
        }

        try {
            bool ran = p->runOneInstruction(id_);
            if (!ran) break;
        }
        catch (const std::exception& e) {
            std::cerr << "[Core-" << id_ << "] Process " << p->getName() << " terminated with exception: " << e.what() << std::endl;
            break;
        }

        globalCpuTicks.fetch_add(1);
        scheduler->updateCoreUtilization(id_, 1);
        executed++;

        if (delayPerExec_ == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        else {
            uint64_t targetTick = globalCpuTicks.load() + delayPerExec_;
            while (globalCpuTicks.load() < targetTick) {
                std::this_thread::yield();
            }
        }
    }

    if (p->isFinished()) {
        if (scheduler) scheduler->addFinishedProcess(p);
    }
    else if (executed >= quantum) {
        if (scheduler) scheduler->requeueProcess(p);
    }

    busy_ = false;
    runningProcess = nullptr;
}
