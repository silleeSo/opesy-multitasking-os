// main.cpp
#include "cli/Console.h"
#include "process/Process.h"
#include "cpu/Core.h"
#include "cpu/Scheduler.h"
#include "common/GlobalState.h"

int main() {
    // Seed the random number generator
    srand(static_cast<unsigned int>(time(nullptr)));

    Console cli;
    cli.run(); // Start the command-line interface
    return 0;
}