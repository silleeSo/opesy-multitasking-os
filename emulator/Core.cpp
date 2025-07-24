#include "Core.h"
#include "Scheduler.h"
#include "GlobalState.h"
#include <iostream>

Core::Core(int id, Scheduler* scheduler, uint64_t delayPerExec)
    : id_(id), busy_(false), scheduler(scheduler), delayPerExec_(delayPerExec) {}

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
        worker_.join();  // Ensure previous thread is cleanly joined
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
    uint64_t executed = 0;

    while (busy_.load() && !p->isFinished() && executed < quantum) {
        if (p->isSleeping()) {
            if (scheduler) scheduler->requeueProcess(p);
            break;
        }

        bool ran = p->runOneInstruction(id_);
        if (!ran) break;

        // Tick only if instruction was executed
        globalCpuTicks.fetch_add(1);
        scheduler->updateCoreUtilization(id_, 1);  // 1 tick of busy CPU time

        executed++;

        // Apply short artificial delay for debug visibility
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

    //  Moved outside of the delay block
    if (p->isFinished()) {
        if (scheduler) scheduler->addFinishedProcess(p);
    }
    else if (executed >= quantum) {
        if (scheduler) scheduler->requeueProcess(p);
    }

    busy_ = false;
    runningProcess = nullptr;
}