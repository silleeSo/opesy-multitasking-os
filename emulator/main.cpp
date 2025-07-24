// main.cpp
#include "Console.h"
#include "Process.h"
#include "Core.h"
#include "Scheduler.h" // Needs to be included since Console now creates Scheduler
#include "GlobalState.h" // Global CPU ticks access

int main() {

    // Seed the random number generator
    srand(static_cast<unsigned int>(time(nullptr)));

    Console cli;
    cli.run(); // Start the command-line interface
    return 0;
}