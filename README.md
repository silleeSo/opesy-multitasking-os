# Multitasking-OS-Emulator-CSOPESY

An operating system emulator written in C++ simulating multi-core CPU execution, scheduling algorithms (FCFS, Round-Robin), virtual memory paging, FIFO page replacement, backing store swapping, and a process shell interface.

---

## Folder Hierarchy

```
opesy-multitasking-os/
│
├── emulator.sln                   # Visual Studio 2022 Solution
├── CMakeLists.txt                 # Modern CMake build configuration
├── README.md
├── .gitignore
│
├── config/                        # Configuration directory
│   └── config.txt                 # OS specification and memory parameters
│
├── logs/                          # Runtime log outputs (tracked via .gitkeep)
│   ├── csopesy-backing-store.txt  # Page eviction backing store log
│   ├── csopesy-log.txt            # System utilization report log
│   └── csopesy-vmstat.txt         # Virtual memory statistics log
│
├── src/                           # Subsystem-modular source code
│   ├── main.cpp                   # Application entry point
│   │
│   ├── cli/                       # Command-line interface & virtual screens
│   │   ├── Console.h / .cpp       # Main REPL shell and command dispatch
│   │   └── Screen.h               # Virtual per-process screen loop
│   │
│   ├── cpu/                       # CPU execution & scheduling
│   │   ├── Core.h / .cpp          # Concurrent CPU core worker threads
│   │   └── Scheduler.h / .cpp     # FCFS / Round-Robin process scheduler
│   │
│   ├── memory/                    # Memory management & paging
│   │   ├── MainMemory.h / .cpp    # Physical frames and frame table
│   │   └── MemoryManager.h / .cpp # Page allocation, FIFO eviction, translation
│   │
│   ├── process/                   # Process Control Block & instructions
│   │   └── Process.h / .cpp       # Instructions, symbol table, page table
│   │
│   └── common/                    # Shared synchronization primitives & globals
│       ├── GlobalState.h / .cpp   # Global CPU clock tick counter
│       └── ThreadedQueue.h        # Thread-safe blocking queue (TSQueue)
│
└── emulator/                      # Visual Studio project files
    ├── emulator.sln
    ├── emulator.vcxproj
    └── emulator.vcxproj.filters
```

---

## How to Build & Run

### Option 1: Visual Studio 2022 (Recommended)
1. Open `emulator.sln` at the root directory (or `emulator/emulator.sln`).
2. Select your build configuration (`Debug` or `Release`, `x64`).
3. Press **F5** (or **Ctrl+F5**) to build and run the solution.

### Option 2: CMake
```bash
cmake -B build
cmake --build build --config Release
cd build
./emulator
```

---

## Getting Started

1. When the emulator starts, type:
   ```bash
   initialize
   ```
   This loads configuration parameters from `config/config.txt` and starts global CPU clock ticks.

2. Type `help` to view all available commands:
   - `initialize`: Initialize the OS specifications (must run first).
   - `process-smi`: Display high-level CPU and memory utilization.
   - `vmstat`: Display detailed virtual memory statistics.
   - `screen -ls`: List active running processes and finished processes.
   - `screen -s <name> <size>`: Create a new process with random instructions.
   - `screen -c <name> <size> "<instructions>"`: Create a process with custom instructions.
   - `screen -r <name>`: Attach to a process screen (run `process-smi` inside, `exit` to return).
   - `scheduler-start`: Start automatic periodic generation of processes.
   - `scheduler-stop`: Stop process generation and stop the scheduler.
   - `report-util`: Save a CPU and memory utilization report to `logs/csopesy-log.txt`.
   - `clear`: Clear the terminal screen.
   - `exit`: Shutdown the emulator gracefully after waiting for active tasks.

---

## Configuration (`config/config.txt`)

| Parameter | Description |
| :--- | :--- |
| `num-cpu` | Number of concurrent CPU cores (threads) |
| `scheduler` | Scheduling algorithm (`"rr"` for Round-Robin, `"fcfs"` for First-Come First-Served) |
| `quantum-cycles` | Time slice quantum cycles (used in Round-Robin) |
| `batch-process-freq` | Frequency (in CPU ticks) for dummy process generation |
| `min-ins` | Minimum instructions per generated process |
| `max-ins` | Maximum instructions per generated process |
| `delay-per-exec` | Simulated delay per instruction execution (ticks) |
| `max-overall-mem` | Total physical memory in bytes (must be power of 2) |
| `mem-per-frame` | Size of each physical frame in bytes (must be power of 2) |
| `min-mem-per-proc` | Minimum memory allocation for a process (must be power of 2) |
| `max-mem-per-proc` | Maximum memory allocation for a process (must be power of 2) |
