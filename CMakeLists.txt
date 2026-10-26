cmake_minimum_required(VERSION 3.12)
project(Multitasking_OS_Emulator_CSOPESY VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# Source files
set(SOURCES
    src/main.cpp
    src/cli/Console.cpp
    src/common/GlobalState.cpp
    src/cpu/Core.cpp
    src/cpu/Scheduler.cpp
    src/memory/MainMemory.cpp
    src/memory/MemoryManager.cpp
    src/process/Process.cpp
)

# Header files (for IDE grouping)
set(HEADERS
    src/cli/Console.h
    src/cli/Screen.h
    src/common/GlobalState.h
    src/common/ThreadedQueue.h
    src/cpu/Core.h
    src/cpu/Scheduler.h
    src/memory/MainMemory.h
    src/memory/MemoryManager.h
    src/process/Process.h
)

add_executable(emulator ${SOURCES} ${HEADERS})

find_package(Threads REQUIRED)
target_link_libraries(emulator PRIVATE Threads::Threads)

# Copy config.txt to the build output directory
add_custom_command(TARGET emulator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/config/config.txt"
    "$<TARGET_FILE_DIR:emulator>/config.txt"
)
