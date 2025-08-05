#pragma once
#include <ctime>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <cstdlib>
#include <string>
#include <thread>
#include <memory>
#include <vector>

#include "Scheduler.h"
#include "Screen.h"
#include "Process.h"
#include "GlobalState.h"
#include "MainMemory.h"
#include "MemoryManager.h"

#ifdef _WIN32
#include <windows.h>
#endif
using namespace std;

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
    void run() {
        clearScreen();
        string line;
        while (true) {
            cout << "root:\\> ";
            if (!getline(cin, line)) break;
            if (line == "exit") break;
            handleCommand(line);
        }
        cout << "Exiting...\n";
    }

    ~Console() {
        if (scheduler_) {
            scheduler_->stopProcessGeneration(); 

            std::cout << "\nWaiting for all processes to finish before exiting...\n";
            scheduler_->waitUntilAllDone(); 
            std::cout << "All processes finished. Shutting down scheduler.\n";
            scheduler_->stop();
        }

        if (cpuTickThread.joinable()) {
            cpuTickThread.detach(); 
        }
    }

private:
    void printHeader() {
        cout << " ,-----. ,---.   ,-----. ,------. ,------. ,---.,--.   ,--.  " << endl;
        cout << "'  .--./'   .-' '  .-.  '|  .--. '|  .---''   .-'\\  `.'  /  " << endl;
        cout << "|  |    `.  `-. |  | |  ||  '--' ||  `--, `.  `-. '.    /   " << endl;
        cout << "'  '--'\\.-'    |'  '-'  '|  | --' |  `---..-'    |  |  |    " << endl;
        cout << " `-----'`-----'  `-----' `--'     `------'`-----'   `--'     " << endl;
        cout << "\nWelcome to CSOPESY Emulator!" << endl;
        cout << "Developers: Group 12 Ariaga, Guillarte, Llorando, So" << endl;
        cout << "Last updated: " << getCurrentTimestamp() << endl;
        cout << "Type 'help' to see available commands\n";
    }

    void clearScreen() {
#ifdef _WIN32
        system("cls");
#endif
        printHeader();
    }

    string getCurrentTimestamp() {
        time_t now = time(nullptr);
        tm localtm{};
#ifdef _WIN32
        localtime_s(&localtm, &now);
#else
        localtime_r(&now, &localtm);
#endif
        char buf[64];
        strftime(buf, sizeof(buf), "%m/%d/%Y, %I:%M:%S %p", &localtm);
        return string(buf);
    }

    void startCpuTickThread() {
        if (cpuTickThread.joinable()) {
            cpuTickThread.detach();
        }
        cpuTickThread = std::thread([]() {
            while (true) {
                globalCpuTicks++;
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
            });
        cpuTickThread.detach();
        cout << "CPU tick thread started." << endl;
    }

    bool isPowerOfTwo(int n) {
        return (n > 0) && ((n & (n - 1)) == 0);
    }

    bool isValidMemorySize(int size) {
        bool inRange = (size >= 64 && size <= 65536);
        return isPowerOfTwo(size) && inRange;
    }

    void handleProcessSmiCommand() {
        cout << "+--------------------------------------------------+" << endl;
        cout << "| PROCESS-SMI V01.00   Driver Version: 01.00       |" << endl;
        cout << "+--------------------------------------------------+" << endl;

        double cpuUtil = scheduler_->getCpuUtilization();
        cout << "| CPU-Util: " << left << setw(33) << (to_string(cpuUtil) + "%") << "|" << endl;

        int totalMemBytes = mainMemory_->getTotalMemoryBytes();
        int usedFrames = mainMemory_->getUsedFrames();
        int frameSize = mainMemory_->getFrameSize();
        int usedMemBytes = usedFrames * frameSize;
        double memUtil = (totalMemBytes > 0) ? (static_cast<double>(usedMemBytes) / totalMemBytes) * 100.0 : 0.0;

        string memUsage = to_string(usedMemBytes) + "B / " + to_string(totalMemBytes) + "B";
        cout << "| Memory Usage: " << left << setw(29) << memUsage << "|" << endl;
        cout << "| Memory Util:  " << left << setw(28) << (to_string(memUtil) + "%") << "|" << endl;
        cout << "+--------------------------------------------------+" << endl;

        cout << "Running processes and memory usage:" << endl;
        auto runningProcs = scheduler_->getRunningProcesses();
        if (runningProcs.empty()) {
            cout << "  No processes currently running." << endl;
        }
        else {
            for (const auto& p : runningProcs) {
                cout << "  " << left << setw(15) << p->getName() << p->getAllocatedMemory() << "B" << endl;
            }
        }
        cout << "+--------------------------------------------------+" << endl;
    }

    void handleVmstatCommand() {
        int totalMemBytes = mainMemory_->getTotalMemoryBytes();
        int usedFrames = mainMemory_->getUsedFrames();
        int frameSize = mainMemory_->getFrameSize();
        int usedMemBytes = usedFrames * frameSize;
        int freeMemBytes = totalMemBytes - usedMemBytes;

        uint64_t totalTicks = globalCpuTicks.load();
        uint64_t activeTicks = scheduler_->getActiveCpuTicks();
        uint64_t idleTicks = totalTicks - activeTicks;

        int pagedIn = memoryManager_->getPagedInCount();
        int pagedOut = memoryManager_->getPagedOutCount();

        cout << "\n+=======================================================================+\n";
        cout << "|                         VIRTUAL MEMORY STATISTICS                     |\n";
        cout << "+=======================================================================+\n";

        cout << "+-------------------------------+---------------------------------------+\n";
        cout << "| Metric                        | Value                                 |\n";
        cout << "+-------------------------------+---------------------------------------+\n";

        cout << "| Total Memory (bytes)          | " << right << setw(38) << totalMemBytes << "|\n";
        cout << "| Used Memory (bytes)           | " << right << setw(38) << usedMemBytes << "|\n";
        cout << "| Free Memory (bytes)           | " << right << setw(38) << freeMemBytes << "|\n";
        cout << "| Frame Size (bytes)            | " << right << setw(38) << frameSize << "|\n";

        cout << "| CPU Idle Ticks                | " << right << setw(38) << idleTicks << "|\n";
        cout << "| CPU Active Ticks              | " << right << setw(38) << activeTicks << "|\n";
        cout << "| CPU Total Ticks               | " << right << setw(38) << totalTicks << "|\n";

        cout << "| Pages Paged In                | " << right << setw(38) << pagedIn << "|\n";
        cout << "| Pages Paged Out               | " << right << setw(38) << pagedOut << "|\n";

        cout << "+=======================================================================+\n\n";
    }



    void handleCommand(const string& line) {
        clearScreen();
        string trimmedLine = line;
        size_t first = trimmedLine.find_first_not_of(' ');
        if (string::npos == first) trimmedLine.clear();
        else trimmedLine = trimmedLine.substr(first, (trimmedLine.find_last_not_of(' ') - first + 1));


        if (trimmedLine == "help") {
            cout << "\nAvailable commands:" << endl;
            cout << "- initialize: Initialize the specifications of the OS (must be called first)" << endl;
            cout << "- process-smi: Display high-level CPU and memory utilization" << endl;
            cout << "- vmstat: Display detailed virtual memory statistics" << endl;
            cout << "- screen -ls: Show active and finished processes" << endl;
            cout << "- screen -s <name> <size>: Create a new process with random instructions" << endl;
            cout << "- screen -c <name> <size> \"<instr>\": Create a new process with custom instructions" << endl;
            cout << "- screen -r <name>: Attach to an existing process screen" << endl;
            cout << "- scheduler-start: Start generating dummy processes and scheduling" << endl;
            cout << "- scheduler-stop: Stop generating dummy processes" << endl;
            cout << "- report-util: Generate CPU utilization report to file" << endl;
            cout << "- clear: Clear the screen" << endl;
            cout << "- exit: Exit the program" << endl;
        }
        else if (trimmedLine == "clear") { clearScreen(); return; }
        else if (trimmedLine == "initialize" && !initialized_) {
            if (loadConfigFile("config.txt")) {
                initialized_ = true;
                cout << "\nLoaded configuration from config.txt:" << endl;
                cout << "  num-cpu: " << cfg_.num_cpu << endl;
                cout << "  scheduler: " << cfg_.scheduler << endl;
                cout << "  quantum-cycles: " << cfg_.quantum_cycles << endl;
                cout << "  batch-process-freq: " << cfg_.batch_process_freq << endl;
                cout << "  min-ins: " << cfg_.min_ins << endl;
                cout << "  max-ins: " << cfg_.max_ins << endl;
                cout << "  delay-per-exec: " << cfg_.delay_per_exec << endl;
                cout << "  max-overall-mem: " << cfg_.max_overall_mem << endl;
                cout << "  mem-per-frame: " << cfg_.mem_per_frame << endl;
                cout << "  min-mem-per-proc: " << cfg_.min_mem_per_proc << endl;
                cout << "  max-mem-per-proc: " << cfg_.max_mem_per_proc << endl;
                cout << endl;

                // 1. Create MainMemory
                mainMemory_ = std::make_unique<MainMemory>(cfg_.max_overall_mem, cfg_.mem_per_frame);

                // 2. Create MemoryManager first, it no longer needs the scheduler to be created
                memoryManager_ = std::make_unique<MemoryManager>(*mainMemory_, cfg_.min_mem_per_proc, cfg_.max_mem_per_proc, cfg_.mem_per_frame);

                // 3. Now create Scheduler, passing the valid MemoryManager reference
                scheduler_ = std::make_unique<Scheduler>(cfg_.num_cpu, cfg_.scheduler, cfg_.quantum_cycles,
                    cfg_.batch_process_freq, cfg_.min_ins, cfg_.max_ins,
                    cfg_.delay_per_exec, *memoryManager_, cfg_.mem_per_frame);

                // 4. Finally, link the MemoryManager back to the Scheduler using the new setter
                memoryManager_->setScheduler(scheduler_.get());

                scheduler_->start();
                startCpuTickThread();
            }
            else {
                cout << "Initialization failed. Check config.txt\n";
            }
            return;
        }
        else if (!initialized_) {
            cout << "Error: Specifications have not yet been initialized! Type 'initialize' first." << endl;
        }
        else {
            if (trimmedLine.rfind("screen -s ", 0) == 0) {
                std::stringstream ss(trimmedLine.substr(10));
                std::string processName;
                int memorySize;

                if (ss >> processName && ss >> memorySize) {
                    if (isValidMemorySize(memorySize)) {
                        auto newProcess = make_shared<Process>(scheduler_->getNextProcessId(), processName, memoryManager_.get());
                        newProcess->setAllocatedMemory(memorySize);

                        scheduler_->submit(newProcess);
                        cout << "Process '" << processName << "' created and submitted." << endl;
                    }
                    else {
                        cout << "Invalid memory allocation: Size must be a power of 2 between 64 and 65536." << endl;
                    }
                }
                else {
                    cout << "Usage: screen -s <process_name> <process_memory_size>" << endl;
                }
            }
            else if (trimmedLine.rfind("screen -c ", 0) == 0) {
                std::stringstream ss(trimmedLine.substr(10));
                std::string processName;
                int memorySize;
                std::string instructions;

                if (ss >> processName && ss >> memorySize) {
                    size_t firstQuote = trimmedLine.find('\"');
                    if (firstQuote != std::string::npos) {
                        size_t lastQuote = trimmedLine.rfind('\"');
                        if (lastQuote != std::string::npos && lastQuote > firstQuote) {
                            instructions = trimmedLine.substr(firstQuote + 1, lastQuote - firstQuote - 1);
                        }
                    }

                    if (instructions.empty()) {
                        cout << "Usage: screen -c <name> <size> \"<instructions>\"" << endl;
                        return;
                    }

                    if (isValidMemorySize(memorySize)) {
                        auto newProcess = make_shared<Process>(scheduler_->getNextProcessId(), processName, memoryManager_.get());
                        newProcess->setAllocatedMemory(memorySize);
                        newProcess->loadInstructionsFromString(instructions);

                        if (newProcess->getTotalInstructions() < 1 || newProcess->getTotalInstructions() > 50) {
                            cout << "Invalid command: Must provide between 1 and 50 instructions." << endl;
                        }
                        else {
                            scheduler_->submit(newProcess);
                            cout << "Process '" << processName << "' created and submitted." << endl;
                            
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                           
                        }
                    }
                    else {
                        cout << "Invalid memory allocation: Size must be a power of 2 between 64 and 65536." << endl;
                    }
                }
                else {
                    cout << "Usage: screen -c <name> <size> \"<instructions>\"" << endl;
                }
            }
            else if (trimmedLine.rfind("screen -r ", 0) == 0) {
                string processName = trimmedLine.substr(10);
                if (processName.empty()) {
                    cout << "Usage: screen -r <process_name>" << endl;
                }
                else {
                    shared_ptr<Process> targetProcess = nullptr;
                    for (const auto& p : scheduler_->getRunningProcesses()) if (p->getName() == processName) targetProcess = p;
                    if (!targetProcess) for (const auto& p : scheduler_->getFinishedProcesses()) if (p->getName() == processName) targetProcess = p;
                    if (!targetProcess) for (const auto& p : scheduler_->getSleepingProcesses()) if (p->getName() == processName) targetProcess = p;

                    if (targetProcess) {
                        if (targetProcess->getTerminationReason() == Process::TerminationReason::MEMORY_VIOLATION) {
                            time_t vt = targetProcess->getViolationTime();
                            tm localtm{};
#ifdef _WIN32
                            localtime_s(&localtm, &vt);
#else
                            localtime_r(&vt, &localtm);
#endif
                            char timebuf[64];
                            strftime(timebuf, sizeof(timebuf), "%H:%M:%S", &localtm);

                            cout << "Process '" << processName << "' shut down due to memory access violation error that occurred at "
                                << timebuf << ". " << targetProcess->getViolationAddress() << " invalid." << endl;
                        }
                        else if (targetProcess->isFinished()) {
                            cout << "Process '" << processName << "' has finished execution." << endl;
                            activeScreen_ = make_unique<Screen>(targetProcess);
                            activeScreen_->run();
                            activeScreen_.reset();
                            clearScreen();
                        }
                        else {
                            activeScreen_ = make_unique<Screen>(targetProcess);
                            activeScreen_->run();
                            activeScreen_.reset();
                            clearScreen();
                        }
                    }
                    else {
                        cout << "Process '" << processName << "' not found." << endl;
                    }
                }
            }
            else if (trimmedLine == "screen -ls") {
                system("cls");
                cout << "CPU utilization:  " << fixed << setprecision(2) << scheduler_->getCpuUtilization() << "%\n";
                cout << "Cores used:       " << scheduler_->getCoresUsed() << '\n';
                cout << "Cores available:  " << scheduler_->getCoresAvailable() << "\n\n";

                cout << "----------------------------\n";
                cout << "Running processes:\n";

                bool anyRunning = false;
                for (int i = 0; i < cfg_.num_cpu; ++i) {
                    auto core = scheduler_->getCore(i);
                    if (core && core->isBusy()) {
                        auto p = core->getRunningProcess();
                        if (p) {
                            time_t now = time(nullptr);
                            tm localtm{};
#ifdef _WIN32
                            localtime_s(&localtm, &now);
#else
                            localtime_r(&now, &localtm);
#endif
                            char timebuf[64];
                            strftime(timebuf, sizeof(timebuf), "%m/%d/%Y %I:%M:%S%p", &localtm);

                            cout << setw(4) << left << p->getName()
                                << " (" << timebuf << ") "
                                << "Core:" << i << " "
                                << p->getCurrentInstructionIndex() << " / "
                                << p->getTotalInstructions() << "\n";
                            anyRunning = true;
                        }
                    }
                }

                if (!anyRunning) {
                    cout << "  No processes currently running.\n";
                }

                cout << "\nFinished processes:\n";
                const auto& finished = scheduler_->getFinishedProcesses();
                if (finished.empty()) {
                    cout << "  No processes have finished.\n";
                }
                else {
                    for (const auto& p : finished) {
                        time_t ft = p->getFinishTime();
                        tm localtm{};
#ifdef _WIN32
                        localtime_s(&localtm, &ft);
#else
                        localtime_r(&ft, &localtm);
#endif
                        char timebuf[64];
                        strftime(timebuf, sizeof(timebuf), "%m/%d/%Y %I:%M:%S%p", &localtm);

                        cout << setw(15) << left << p->getName()
                            << " (" << timebuf << ") "
                            << "Finished "
                            << p->getTotalInstructions() << " / "
                            << p->getTotalInstructions() << "\n";
                    }
                }
                cout << "----------------------------\n";
            }
            else if (trimmedLine == "scheduler-start") {
                scheduler_->startProcessGeneration();
                cout << "Scheduler process generation started." << endl;
            }
            else if (trimmedLine == "scheduler-stop") {
                scheduler_->stopProcessGeneration();
                cout << "Scheduler process generation stopped." << endl;
            }
            else if (trimmedLine == "report-util") {
                generateReport();
            }
            else if (trimmedLine == "process-smi") {
                handleProcessSmiCommand();
            }
            else if (trimmedLine == "vmstat") {
                handleVmstatCommand();
            }
            else {
                cout << "[" << getCurrentTimestamp() << "] Unknown command: " << trimmedLine << '\n';
            }
        }
    }

    void generateReport() {
        ofstream out("csopesy-log.txt");
        if (!out) {
            cout << "Error: Cannot create csopesy-log.txt\n";
            return;
        }

        out << "CSOPESY Emulator Report - " << getCurrentTimestamp() << "\n\n";
        out << "CPU utilization: " << fixed << setprecision(2) << scheduler_->getCpuUtilization() << "%" << endl;
        out << "Cores used: " << scheduler_->getCoresUsed() << endl;
        out << "Cores available: " << scheduler_->getCoresAvailable() << endl;

        out << "\n----------------------------\n";
        out << "Running processes:\n";

        bool anyRunning = false;
        for (int i = 0; i < cfg_.num_cpu; ++i) {
            auto core = scheduler_->getCore(i);
            if (core && core->isBusy()) {
                auto p = core->getRunningProcess();
                if (p) {
                    time_t now = time(nullptr);
                    tm localtm{};
#ifdef _WIN32
                    localtime_s(&localtm, &now);
#else
                    localtime_r(&now, &localtm);
#endif
                    char timebuf[64];
                    strftime(timebuf, sizeof(timebuf), "%m/%d/%Y %I:%M:%S%p", &localtm);

                    out << setw(15) << left << p->getName()
                        << " (" << timebuf << ") "
                        << "Core:" << i << " "
                        << p->getCurrentInstructionIndex() << " / "
                        << p->getTotalInstructions() << "\n";
                    anyRunning = true;
                }
            }
        }
        if (!anyRunning) {
            out << "  No processes currently running.\n";
        }

        out << "\nFinished processes:\n";
        const auto& finished = scheduler_->getFinishedProcesses();
        if (finished.empty()) {
            out << "  No processes have finished.\n";
        }
        else {
            for (const auto& p : finished) {
                time_t ft = p->getFinishTime();
                tm localtm{};
#ifdef _WIN32
                localtime_s(&localtm, &ft);
#else
                localtime_r(&ft, &ft);
#endif
                char timebuf[64];
                strftime(timebuf, sizeof(timebuf), "%m/%d/%Y %I:%M:%S%p", &localtm);

                out << setw(15) << left << p->getName()
                    << " (" << timebuf << ") "
                    << "Finished "
                    << p->getTotalInstructions() << " / "
                    << p->getTotalInstructions() << "\n";
            }
        }

        out << "----------------------------\n";
        cout << "Report written to csopesy-log.txt\n";
    }

    bool loadConfigFile(const string& path) {
        ifstream in(path);
        if (!in) { cout << "config.txt not found!\n"; return false; }

        unordered_map<string, string> kv;
        string k, v;
        while (in >> k >> v) kv[k] = stripQuotes(v);

        try {
            cfg_.num_cpu = stoi(kv.at("num-cpu"));
            cfg_.scheduler = kv.at("scheduler");
            cfg_.quantum_cycles = stoull(kv.at("quantum-cycles"));
            cfg_.batch_process_freq = stoull(kv.at("batch-process-freq"));
            cfg_.min_ins = stoull(kv.at("min-ins"));
            cfg_.max_ins = stoull(kv.at("max-ins"));
            cfg_.delay_per_exec = stoull(kv.at("delay-per-exec"));
            cfg_.max_overall_mem = stoi(kv.at("max-overall-mem"));
            cfg_.mem_per_frame = stoi(kv.at("mem-per-frame"));
            cfg_.min_mem_per_proc = stoi(kv.at("min-mem-per-proc"));
            cfg_.max_mem_per_proc = stoi(kv.at("max-mem-per-proc"));
        }
        catch (...) {
            cout << "Malformed config.txt – missing field or unexpected error\n";
            return false;
        }

        if (!isPowerOfTwo(cfg_.max_overall_mem) || !isPowerOfTwo(cfg_.mem_per_frame) ||
            !isPowerOfTwo(cfg_.min_mem_per_proc) || !isPowerOfTwo(cfg_.max_mem_per_proc)) {
            cout << "Configuration error: All memory sizes (max-overall-mem, mem-per-frame, min-mem-per-proc, max-mem-per-proc) must be a power of 2." << endl;
            return false;
        }

        return true;
    }

    static string stripQuotes(string s) {
        if (!s.empty() && (s.front() == '\"' || s.front() == '\'')) s.erase(0, 1);
        if (!s.empty() && (s.back() == '\"' || s.back() == '\'')) s.pop_back();
        return s;
    }

    Config cfg_;
    bool   initialized_ = false;
    std::unique_ptr<MainMemory> mainMemory_;
    std::unique_ptr<MemoryManager> memoryManager_;
    std::unique_ptr<Scheduler> scheduler_;
    std::unique_ptr<Screen> activeScreen_;
    std::thread cpuTickThread;
};
