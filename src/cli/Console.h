#pragma once
#include <string>
#include <memory>
#include <thread>
#include <cstdint>

#include "cpu/Scheduler.h"
#include "cli/Screen.h"
#include "process/Process.h"
#include "common/GlobalState.h"
#include "memory/MainMemory.h"
#include "memory/MemoryManager.h"

struct Config {
    int          num_cpu = 1;
    std::string  scheduler = "fcfs";
    uint64_t     quantum_cycles = 1;
    uint64_t     batch_process_freq = 1;
    uint64_t     min_ins = 1;
    uint64_t     max_ins = 1;
    uint64_t     delay_per_exec = 0;
    int          max_overall_mem = 16384;
    int          mem_per_frame = 16;
    int          min_mem_per_proc = 1024;
    int          max_mem_per_proc = 4096;
};

class Console {
public:
    Console();
    ~Console();

    void run();

private:
    void printHeader();
    void clearScreen();
    std::string getCurrentTimestamp();
    void startCpuTickThread();
    bool isPowerOfTwo(int n);
    bool isValidMemorySize(int size);

    void handleProcessSmiCommand();
    void handleVmstatCommand();
    void handleCommand(const std::string& line);
    void generateReport();
    bool loadConfigFile(const std::string& path);
    static std::string stripQuotes(std::string s);

    Config cfg_;
    bool   initialized_ = false;
    std::unique_ptr<MainMemory> mainMemory_;
    std::unique_ptr<MemoryManager> memoryManager_;
    std::unique_ptr<Scheduler> scheduler_;
    std::unique_ptr<Screen> activeScreen_;
    std::thread cpuTickThread;
};
