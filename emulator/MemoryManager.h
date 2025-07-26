#pragma once
#include "MainMemory.h"
#include <unordered_map>
#include <string>
#include <fstream>
#include <memory>
#include <vector>
#include <utility>

class Process;

class MemoryManager {
public:
    MemoryManager(MainMemory& mem, int minMemProc, int maxMemProc, int frameSz);

    bool allocateMemory(std::shared_ptr<Process> process, int requestedBytes);
    std::string allocateVariable(std::shared_ptr<Process> process, const std::string& varName);

    bool isAddressInMemory(const std::string& addr);
    uint16_t read(const std::string& addr, std::shared_ptr<Process> p);
    void write(const std::string& addr, uint16_t value, std::shared_ptr<Process> p);

    void evictPage(int index);
    void handlePageFault(std::shared_ptr<Process> p, int pageNum);
    void writeToBackingStore(const std::string& pageId);
    void logMemorySnapshot();

    int getPagedInCount() const;
    int getPagedOutCount() const;

    void deallocate(uint64_t  pid);

    // CHANGED: Dana - Made getRandomMemorySize public so the Scheduler can use it
    int getRandomMemorySize() const;

private:
    MainMemory& memory;
    int minMemPerProc;
    int maxMemPerProc;
    int frameSize;
    int pagedInCount = 0;
    int pagedOutCount = 0;
    int nextPageId = 0;

    std::pair<int, int> translate(std::string logicalAddr, std::shared_ptr<Process> p);

    std::unordered_map<std::string, std::vector<uint16_t>> backingStore_;
};
