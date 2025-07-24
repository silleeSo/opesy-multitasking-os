// Screen.h
#pragma once
#include <iostream>
#include <memory>
#include <string>
#include <vector> // For logs
#include <unordered_map> // For variables
#include "Process.h"
#include "GlobalState.h" // For globalCpuTicks in process-smi display
// Removed using namespace std; to be explicit
/*
    SCREEN OVERVIEW
    - wraps process basically
    - has the ui features of a process
*/

class Screen {
public:
    Screen(std::shared_ptr<Process> proc) : process{ proc } {}

    // Enters the process screen loop.
    void run() {
        clearScreen();
        std::string line;
        while (true) {
            std::cout << process->getName() << ":> "; // Show process name in prompt
            if (!std::getline(std::cin, line)) break;
            if (line == "exit") break;
            handleCommand(line);
        }
        std::cout << "Returning to main menu...\n";
    }

private:
    std::shared_ptr<Process> process;

    // ----- helpers -----

    void clearScreen() {
#ifdef _WIN32
        system("cls");
#else
        system("clear");
#endif
        std::cout << "--- Process Screen for " << process->getName() << " (PID: " << process->getPid() << ") --- (type 'exit' to leave)\n";
        std::cout << "Current Global CPU Tick: " << globalCpuTicks.load() << "\n\n";
    }


    void handleCommand(const std::string& cmd) {
        clearScreen(); // Clear screen on each command to refresh view

        if (cmd == "process-smi") {
            if (process) {
                std::cout << process->smi() << std::endl; // Use the detailed smi method from Process
            }
            else {
                std::cout << "Error: No process attached to this screen.\n";
            }
        }
        else {
            std::cout << "Unknown screen command.\n";
        }
    }
};