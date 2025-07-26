// Console.h
#pragma once
#include <ctime>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <cstdlib>
#include <string>
#include <thread> // For cpuTickThread
#include <memory> // For unique_ptr and shared_ptr
#include <vector>

#include "Scheduler.h"
#include "Screen.h"
#include "Process.h"
#include "GlobalState.h" // Include for globalCpuTicks
#include "MainMemory.h"
#include "MemoryManager.h"

#ifdef _WIN32
#include <windows.h>
#endif
using namespace std;

//CONFIG STRUCT

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
    int          min_mem_per_proc = 1024; // New
    int          max_mem_per_proc = 4096; // New
};


class Console {
public:
    /* Entry‑point (blocking CLI loop) */
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

private:

    void printHeader() {
        cout << " ,-----. ,---.   ,-----. ,------. ,------. ,---.,--.   ,--.  " << endl;
        cout << "'  .--./'   .-' '  .-.  '|  .--. '|  .---''   .-'\\  `.'  /  " << endl;
        cout << "|  |    `.  `-. |  | |  ||  '--' ||  `--, `.  `-. '.    /   " << endl;
        cout << "'  '--'\\.-'    |'  '-'  '|  | --' |  `---..-'    |  |  |    " << endl;
        cout << " `-----'`-----'  `-----' `--'     `------'`-----'   `--'     " << endl;
        cout << "\nWelcome to CSOPESY Emulator!" << endl;
        cout << "Developers: Group 12 Ariaga, Guillarte, Llorando, So" << endl; // Placeholder
        cout << "Last updated: " << getCurrentTimestamp() << endl;
        cout << "Type 'help' to see available commands\n";
    }
    void clearScreen() {
#ifdef _WIN32
        system("cls");
#else
        system("clear");
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

    // New: Function to start the CPU tick thread
    void startCpuTickThread() {
        // Ensure only one tick thread is running
        if (cpuTickThread.joinable()) {
            // Already running or joined, detach it again if it was joined previously
            cpuTickThread.detach();
        }

        cpuTickThread = std::thread([]() {
            while (true) {
                globalCpuTicks++;
                // A very small sleep to prevent 100% CPU usage for the tick thread itself.
                // The actual 'delay-per-exec' busy-waiting happens in Core.
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
            });
        cpuTickThread.detach(); // Let it run independently
        cout << "CPU tick thread started." << endl;
    }


    void handleCommand(const string& line) {
        clearScreen();
        // Trim whitespace from the line for robust command parsing
        string trimmedLine = line;
        size_t first = trimmedLine.find_first_not_of(' ');
        if (string::npos == first) trimmedLine.clear();
        else trimmedLine = trimmedLine.substr(first, (trimmedLine.find_last_not_of(' ') - first + 1));


        if (trimmedLine == "help") {
            cout << "\nAvailable commands:" << endl;
            cout << "- initialize: Initialize the specifications of the OS (must be called first)" << endl;
            cout << "- screen -ls: Show active and finished processes" << endl;
            cout << "- screen -s <process_name>: Create and attach to a new process screen" << endl;
            cout << "- screen -r <process_name>: Attach to an existing process screen" << endl;
            cout << "- scheduler-start: Start generating dummy processes and scheduling" << endl;
            cout << "- scheduler-stop: Stop generating dummy processes" << endl;
            cout << "- report-util: Generate CPU utilization report to file" << endl;
            cout << "- clear: Clear the screen" << endl;
            cout << "- exit: Exit the program" << endl;
        }

        else if (trimmedLine == "clear") { clearScreen(); return; }

        else if (trimmedLine == "initialize" && initialized_ == false) {
            if (loadConfigFile("config.txt")) {
                initialized_ = true;
                cout << "\nLoaded configuration:\n"
                    << "  num-cpu            = " << cfg_.num_cpu << '\n'
                    << "  scheduler          = " << cfg_.scheduler << '\n'
                    << "  quantum-cycles     = " << cfg_.quantum_cycles << '\n'
                    << "  batch_process_freq = " << cfg_.batch_process_freq << '\n'
                    << "  min_ins            = " << cfg_.min_ins << '\n'
                    << "  max_ins            = " << cfg_.max_ins << '\n'
                    << "  delay_per_exec     = " << cfg_.delay_per_exec << '\n'
                    << "  max-overall-mem    = " << cfg_.max_overall_mem << '\n'
                    << "  mem-per-frame      = " << cfg_.mem_per_frame << '\n'
                    << "  min-mem-per-proc   = " << cfg_.min_mem_per_proc << '\n'
                    << "  max-mem-per-proc   = " << cfg_.max_mem_per_proc << '\n';
               
                mainMemory_ = std::make_unique<MainMemory>(
                    cfg_.max_overall_mem, cfg_.mem_per_frame);

                memoryManager_ = std::make_unique<MemoryManager>(
                    *mainMemory_, cfg_.min_mem_per_proc, cfg_.max_mem_per_proc, cfg_.mem_per_frame);

                scheduler_ = std::make_unique<Scheduler>(
                    cfg_.num_cpu,
                    cfg_.scheduler,
                    cfg_.quantum_cycles,
                    cfg_.batch_process_freq,
                    cfg_.min_ins,
                    cfg_.max_ins,
                    cfg_.delay_per_exec,
                    *memoryManager_ 
                );



                scheduler_->start();          // Start the scheduler's main loop
                startCpuTickThread();         // Start the global CPU tick counter
            }
            else {
                cout << "Initialization failed – check config.txt\n";
            }
            return;
        }
        else if (!initialized_) {
            cout << "Error: Specifications have not yet been initialized! Type 'initialize' first." << endl;
        }
        else { // Commands requiring initialization
            if (trimmedLine.rfind("screen -s ", 0) == 0) { // Starts with "screen -s "
                string processName = trimmedLine.substr(trimmedLine.find("screen -s ") + 10);
                if (processName.empty()) {
                    cout << "Usage: screen -s <process_name>" << endl;
                }
                else {
                    // Check if process name already exists
                    bool nameExists = false;
                    for (const auto& p : scheduler_->getRunningProcesses()) {
                        if (p->getName() == processName) {
                            nameExists = true;
                            break;
                        }
                    }
                    if (!nameExists) {
                        for (const auto& p : scheduler_->getFinishedProcesses()) {
                            if (p->getName() == processName) {
                                nameExists = true;
                                break;
                            }
                        }
                    }
                    // Also check sleeping processes
                    if (!nameExists) {
                        for (const auto& p : scheduler_->getSleepingProcesses()) { // Assuming a getter for sleeping processes
                            if (p->getName() == processName) {
                                nameExists = true;
                                break;
                            }
                        }
                    }


                    if (nameExists) {
                        cout << "Error: Process with name '" << processName << "' already exists." << endl;
                    }
                    else {
                        // Create a new process and submit to scheduler
                        // PID will be assigned by scheduler's internal counter or a new mechanism
                        // NEWLY CHANGED: Dana - Modify to accommodate new Process constructor
                        auto newProcess = make_shared<Process>(static_cast<int>(scheduler_->getNextProcessId()), processName, memoryManager_.get());
                        newProcess->genRandInst(cfg_.min_ins, cfg_.max_ins); // Generate instructions
                        scheduler_->submit(newProcess);
                        cout << "Process '" << processName << "' (PID: " << newProcess->getPid() << ") created and submitted." << endl;
                        // Attach to screen
                        activeScreen_ = make_unique<Screen>(newProcess);
                        activeScreen_->run(); // Manually runs the process

                        // ✅ After exiting the screen, check if it finished and add to finished list
                        if (newProcess->isFinished()) {
                            scheduler_->addFinishedProcess(newProcess);
                        }

                        activeScreen_.reset(); // Clear active screen
                        clearScreen();         // Clear screen after returning

                    }
                }
            }
            else if (trimmedLine.rfind("screen -r ", 0) == 0) { // Starts with "screen -r "
                string processName = trimmedLine.substr(trimmedLine.find("screen -r ") + 10);
                if (processName.empty()) {
                    cout << "Usage: screen -r <process_name>" << endl;
                }
                else {
                    shared_ptr<Process> targetProcess = nullptr;
                    // Check running processes
                    for (const auto& p : scheduler_->getRunningProcesses()) {
                        if (p->getName() == processName) {
                            targetProcess = p;
                            break;
                        }
                    }
                    // Check finished processes if not found in running
                    if (!targetProcess) {
                        for (const auto& p : scheduler_->getFinishedProcesses()) {
                            if (p->getName() == processName) {
                                targetProcess = p;
                                break;
                            }
                        }
                    }
                    // Check sleeping processes if not found in running or finished
                    if (!targetProcess) {
                        for (const auto& p : scheduler_->getSleepingProcesses()) { // Assuming a getter for sleeping processes
                            if (p->getName() == processName) {
                                targetProcess = p;
                                break;
                            }
                        }
                    }

                    if (targetProcess) {
                        if (targetProcess->isFinished()) {
                            cout << "Process '" << processName << "' has finished execution." << endl;
                            // Still allow attaching to a finished process screen to view its final state/logs
                            activeScreen_ = make_unique<Screen>(targetProcess);
                            activeScreen_->run(); // Enter process screen loop
                            activeScreen_.reset(); // Clear active screen after exit
                            clearScreen(); // Clear screen after returning from process screen
                        }
                        else {
                            activeScreen_ = make_unique<Screen>(targetProcess);
                            activeScreen_->run(); // Enter process screen loop
                            activeScreen_.reset(); // Clear active screen after exit
                            clearScreen(); // Clear screen after returning from process screen
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
                            // Format timestamp
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
                    localtime_r(&localtm, &now);
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
                localtime_r(&localtm, &ft);
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

    //CONFIG LOADER
    static string stripQuotes(string s) {
        if (!s.empty() && (s.front() == '\"' || s.front() == '\'')) s.erase(0, 1);
        if (!s.empty() && (s.back() == '\"' || s.back() == '\'')) s.pop_back();
        return s;
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
        catch (const out_of_range& oor) {
            (void)oor; // Suppress unused variable warning
            cout << "Malformed config.txt – missing field or value out of range (stoull conversion): " << oor.what() << '\n';
            return false;
        }
        catch (const invalid_argument& ia) {
            cout << "Malformed config.txt – invalid argument for conversion: " << ia.what() << '\n';
            return false;
        }
        catch (...) {
            cout << "Malformed config.txt – missing field or unexpected error\n";
            return false;
        }

        /* Basic range checks */
        if (cfg_.num_cpu < 1 || cfg_.num_cpu > 128) {
            cout << "num-cpu out of range (1–128)\n"; return false;
        }
        if (cfg_.scheduler != "fcfs" && cfg_.scheduler != "rr") {
            cout << "scheduler must be 'fcfs' or 'rr'\n"; return false;
        }
        // Additional range checks for uint64_t parameters as per spec.
        // For uint64_t, values are generally positive. Max limits are 2^32, but stoull already handles max uint64_t.
        // We only need to check against 1 for minimums if they are specified in the config.
        if (cfg_.quantum_cycles < 1 && cfg_.scheduler == "rr") { // Quantum must be at least 1 for RR
            cout << "quantum-cycles must be at least 1 for Round Robin scheduler\n"; return false;
        }
        if (cfg_.batch_process_freq < 1) {
            cout << "batch-process-freq must be at least 1\n"; return false;
        }
        if (cfg_.min_ins < 1 || cfg_.max_ins < 1 || cfg_.min_ins > cfg_.max_ins) {
            cout << "min-ins and max-ins must be at least 1, and min-ins <= max-ins\n"; return false;
        }

        return true;
    }


    Config cfg_;
    bool   initialized_ = false;
    std::unique_ptr<MainMemory> mainMemory_;
    std::unique_ptr<MemoryManager> memoryManager_;

    std::unique_ptr<Scheduler> scheduler_;          // created after init
    std::unique_ptr<Screen> activeScreen_;          // one attached screen at a time
    std::thread cpuTickThread; // Thread for the global CPU tick counter
};