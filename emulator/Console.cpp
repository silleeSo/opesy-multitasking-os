#define NOMINMAX // Add this line BEFORE any includes that might bring in Windows.h
#include "Console.h"
#include <sstream>
#include <limits> // For numeric_limits
#include <random> // For random process names
#include <algorithm> // Include for std::min/max if you use them explicitly later and NOMINMAX
// Though numeric_limits<>::max() is a function, not a macro, the conflict often arises.

// Helper to generate unique process names like p01, p02, etc. 
string generateProcessName(int id) {
    stringstream ss;
    ss << "p" << setw(2) << setfill('0') << id;
    return ss.str();
}

void Console::handleCommand(const string& line) {
    clearScreen(); // Clear screen for each command output

    if (line == "help") {
        cout << "\nAvailable commands:" << endl;
        cout << "- initialize: initialize the specifications of the OS" << endl;
        cout << "- screen -ls: Show active and finished processes" << endl;
        cout << "- screen -s <process_name>: Create a new process" << endl;
        cout << "- screen -r <process_name>: Attach to a process screen" << endl;
        cout << "- scheduler-start: Start scheduler threads and process generation" << endl;
        cout << "- scheduler-stop: Stop scheduler threads and process generation" << endl;
        cout << "- report-util: Generate CPU utilization report to csopesy-log.txt" << endl;
        cout << "- clear: Clear the screen" << endl;
        cout << "- exit: Exit the program" << endl;
        cout << "Note: you must call initialize before any functional CLI command" << endl;
    }
    else if (line == "clear") {
        return; // clearScreen already called at start of handleCommand
    }
    else if (line == "initialize") {
        if (!initialized_) {
            if (loadConfigFile("config.txt")) {
                initialized_ = true;
                // Initialize scheduler AFTER config is loaded 
                scheduler_ = std::make_unique<Scheduler>(cfg_.num_cpu, cfg_.scheduler, cfg_.quantum_cycles, cfg_.batch_process_freq, cfg_.min_ins, cfg_.max_ins, cfg_.delay_per_exec);

                cout << "\nLoaded configuration:\n"
                    << "  num-cpu            = " << cfg_.num_cpu << '\n'
                    << "  scheduler          = " << cfg_.scheduler << '\n'
                    << "  quantum-cycles     = " << cfg_.quantum_cycles << '\n'
                    << "  batch-process-freq = " << cfg_.batch_process_freq << '\n'
                    << "  min-ins            = " << cfg_.min_ins << '\n'
                    << "  max-ins            = " << cfg_.max_ins << '\n'
                    << "  delay-per-exec     = " << cfg_.delay_per_exec << '\n';
            }
            else {
                cout << "Initialization failed – check config.txt\n";
            }
        }
        else {
            cout << "System already initialized.\n";
        }
    }
    else {
        if (!initialized_) {
            cout << "Specifications have not yet been initialized! Please run 'initialize' first." << endl; // 
            return;
        }

        // Handle commands requiring initialization
        if (line == "screen -ls") { // 
            generateReportToFile(cout); // Reuse report logic for console output 
        }
        else if (line.rfind("screen -s ", 0) == 0) { // Starts with "screen -s " 
            string processName = line.substr(string("screen -s ").length());
            if (processName.empty()) {
                cout << "Usage: screen -s <process_name>\n";
            }
            else {
                // Check if process name already exists
                auto it = find_if(allProcesses_.begin(), allProcesses_.end(),
                    [&](const shared_ptr<Process>& p) { return p->getName() == processName; });
                if (it != allProcesses_.end()) {
                    cout << "Process with name '" << processName << "' already exists. Please choose a different name.\n";
                }
                else {
                    currentProcessId_++; // Increment for new unique PID
                    auto newProcess = make_shared<Process>(currentProcessId_, processName, cfg_.min_ins, cfg_.max_ins);
                    allProcesses_.push_back(newProcess);
                    scheduler_->submit(newProcess); // Submit to scheduler for execution

                    activeScreen_ = make_shared<Screen>(newProcess);
                    activeScreen_->run(); // Enter the process screen 
                    activeScreen_.reset(); // Clear active screen after exiting
                    clearScreen(); // Clear screen after returning from process screen
                    printHeader(); // Reprint header for main menu
                }
            }
        }
        else if (line.rfind("screen -r ", 0) == 0) { // 
            string processName = line.substr(string("screen -r ").length());
            if (processName.empty()) {
                cout << "Usage: screen -r <process_name>\n";
            }
            else {
                auto it = find_if(allProcesses_.begin(), allProcesses_.end(),
                    [&](const shared_ptr<Process>& p) { return p->getName() == processName; });
                if (it != allProcesses_.end()) {
                    if (!(*it)->isFinished()) { // Only attach if not finished 
                        activeScreen_ = make_shared<Screen>(*it);
                        activeScreen_->run(); // Enter the process screen 
                        activeScreen_.reset(); // Clear active screen after exiting
                        clearScreen(); // Clear screen after returning from process screen
                        printHeader(); // Reprint header for main menu
                    }
                    else {
                        cout << "Process '" << processName << "' has finished execution and cannot be accessed.\n"; // 
                    }
                }
                else {
                    cout << "Process '" << processName << "' not found.\n"; // 
                }
            }
        }
        else if (line == "scheduler-start") { // 
            if (scheduler_) {
                // Pass a lambda to generate processes for scheduler to use
                scheduler_->startProcessGeneration([this]() {
                    currentProcessId_++;
                    string name = generateProcessName(currentProcessId_); // 
                    auto p = make_shared<Process>(currentProcessId_, name, cfg_.min_ins, cfg_.max_ins);
                    allProcesses_.push_back(p);
                    return p;
                    });
                scheduler_->start(); // Start the scheduler loop 
                cout << "Scheduler started and continuous process generation enabled.\n";
            }
            else {
                cout << "Scheduler not initialized. Run 'initialize' first.\n";
            }
        }
        else if (line == "scheduler-stop") { // 
            if (scheduler_) {
                scheduler_->stopProcessGeneration(); // Stop generating new processes 
                scheduler_->stop(); // Stop the scheduler loop
                cout << "Scheduler stopped and continuous process generation disabled.\n";
            }
            else {
                cout << "Scheduler not initialized.\n";
            }
        }
        else if (line == "report-util") { // 
            generateReport(); // Will write to file
        }
        else {
            cout << "[" << getCurrentTimestamp() << "] Unknown command: " << line << '\n';
        }
    }
}

// Reusable function to generate report content for both console and file
void Console::generateReportToFile(ostream& out) {
    if (!scheduler_) {
        out << "Scheduler not initialized. Cannot generate report.\n";
        return;
    }

    // CPU utilization, cores used, and cores available 
    out << "CPU utilization: " << scheduler_->getCPUUtilization() << "%\n"; // Placeholder for actual calculation
    out << "Cores used: " << scheduler_->getCoresUsed() << '\n';
    out << "Cores available: " << scheduler_->getCoresAvailable() << '\n';

    out << "\nRunning processes:\n"; // 
    for (const auto& p : allProcesses_) {
        if (!p->isFinished() && scheduler_->isProcessRunning(p)) { // Check if the process is known by the scheduler to be running on a core
            // Format: process05 (01/18/2024 09:15:22AM) Core: 0 1235 / 5676 
            out << p->getName() << " (" << p->getCreationTimestamp() << ") "
                << "Core: " << scheduler_->getCoreIdForProcess(p) << " "
                << p->getCurrentInstructionLine() << " / " << p->getTotalLinesOfCode() << '\n';
        }
    }

    out << "\nFinished processes:\n"; // 
    for (const auto& p : allProcesses_) {
        if (p->isFinished()) {
            // Format: process01 (01/18/2024 09:00:21AM) Finished 5876/5876 
            out << p->getName() << " (" << p->getCreationTimestamp() << ") "
                << "Finished " << p->getCurrentInstructionLine() << "/" << p->getTotalLinesOfCode() << '\n';
        }
    }
}

void Console::generateReport() {
    ofstream out("csopesy-log.txt"); // 
    if (!out) {
        cout << "Cannot create csopesy-log.txt\n";
        return;
    }
    generateReportToFile(out); // Call helper to write to file
    out.close();
    cout << "Report generated at C:/csopesy-log.txt!\n"; // 
}

//CONFIG LOADER 
bool Console::loadConfigFile(const string& path) {
    ifstream in(path);
    if (!in) { cout << "config.txt not found!\n"; return false; } // 

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
    }
    catch (...) {
        cout << "Malformed config.txt – missing or invalid field\n";
        return false;
    }

    /* Basic range checks  */
    if (cfg_.num_cpu < 1 || cfg_.num_cpu > 128) {
        cout << "num-cpu out of range (1–128)\n"; return false;
    }
    if (cfg_.scheduler != "fcfs" && cfg_.scheduler != "rr") {
        cout << "scheduler must be 'fcfs' or 'rr'\n"; return false;
    }
    // Quantum cycles [1, 2^32]
    // Using uint32_t max as a practical limit for 2^32 as specified in the document
    if (cfg_.quantum_cycles < 1 || cfg_.quantum_cycles > std::numeric_limits<uint32_t>::max()) { // Using std::numeric_limits
        cout << "quantum-cycles out of range (1–2^32)\n"; return false;
    }
    // Batch process frequency [1, 2^32]
    if (cfg_.batch_process_freq < 1 || cfg_.batch_process_freq > std::numeric_limits<uint32_t>::max()) { // Using std::numeric_limits
        cout << "batch-process-freq out of range (1–2^32)\n"; return false;
    }
    // Min/Max instructions [1, 2^32]
    if (cfg_.min_ins < 1 || cfg_.min_ins > std::numeric_limits<uint32_t>::max()) { // Using std::numeric_limits
        cout << "min-ins out of range (1–2^32)\n"; return false;
    }
    if (cfg_.max_ins < 1 || cfg_.max_ins > std::numeric_limits<uint32_t>::max()) { // Using std::numeric_limits
        cout << "max-ins out of range (1–2^32)\n"; return false;
    }
    if (cfg_.min_ins > cfg_.max_ins) {
        cout << "min-ins cannot be greater than max-ins\n"; return false;
    }
    // Delay per exec [0, 2^32]
    if (cfg_.delay_per_exec > std::numeric_limits<uint32_t>::max()) { // Using std::numeric_limits
        cout << "delay-per-exec out of range (0–2^32)\n"; return false;
    }

    return true;
}