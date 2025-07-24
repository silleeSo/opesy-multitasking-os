// GlobalState.h
#pragma once
#include <atomic>
#include <cstdint>

// Global atomic counter for CPU ticks
extern std::atomic<uint64_t> globalCpuTicks;