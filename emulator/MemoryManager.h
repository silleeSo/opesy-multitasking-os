#pragma once
#include "MainMemory.h"
#include <unordered_map>
#include <string>
#include <fstream>
#include <memory>
#include <utility>
#include <memory>


class Process;

class MemoryManager {
public:
    MemoryManager(MainMemory& mem, int frameSize);

    bool allocateMemory(std::shared_ptr<Process> process, int requestedBytes);
    std::string allocateVariable(std::shared_ptr<Process> process, const std::string& varName);
    bool loadPage(const std::string& pageId);
    void evictPage(int frameIndex);
    void writeToBackingStore(const std::string& pageId);
    void loadFromBackingStore(const std::string& pageId);

    bool isAddressInMemory(const std::string& address);
    uint16_t read(const std::string& address, std::shared_ptr<Process> p);
    void write(const std::string& address, uint16_t value, std::shared_ptr<Process> p);

    void logMemorySnapshot();
    int getPagedInCount() const;
    int getPagedOutCount() const;
    std::pair<int, int> translate(std::string logicalAddr, std::shared_ptr<Process> p);


private:
    MainMemory& memory;
    int frameSize;
    int nextPageId;
    int pagedInCount = 0;
    int pagedOutCount = 0;
};
