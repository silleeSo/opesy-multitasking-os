#pragma once
#include "MainMemory.h"
#include <vector>
#include <string>
#include <memory>
#include <queue>
#include <mutex>
#include <unordered_map>

class Process;

class MemoryManager {
public:
    MemoryManager(MainMemory& mem, int minMemProc, int maxMemProc, int frameSz);
    bool allocateMemory(std::shared_ptr<Process> process, int requestedBytes);
    void deallocate(uint64_t pid);
    uint16_t read(const std::string& addr, std::shared_ptr<Process> p);
    void write(const std::string& addr, uint16_t value, std::shared_ptr<Process> p);
    void preloadPages(std::shared_ptr<Process> process, int startPage, int numPages);
    int getRandomMemorySize() const;
    int getPagedInCount() const;
    int getPagedOutCount() const;

private:
    void initializeBackingStoreFile();
    std::pair<int, int> translate(std::string logicalAddr, std::shared_ptr<Process> p);
    void handlePageFault(std::shared_ptr<Process> p, int pageNum);
    void evictPage(int frameIndex);
    void evictPageToFile(const std::string& pageId, const std::string& logicalBaseAddr, const std::vector<uint16_t>& pageData);
    std::vector<uint16_t> loadPageFromFile(const std::string& pageId);
    std::string allocateVariable(std::shared_ptr<Process> process, const std::string& varName);
    bool isAddressInMemory(const std::string& addr);
    int getVictimFrame_FIFO();

    MainMemory& memory;
    std::string backingStoreFileName = "csopesy-backing-store.txt";
    int minMemPerProc;
    int maxMemPerProc;
    int frameSize;
    int pagedInCount = 0;
    int pagedOutCount = 0;
    std::queue<int> frame_fifo_queue_;

    // This map tracks which process owns which frame, critical for eviction.
    std::unordered_map<int, std::shared_ptr<Process>> frame_owners_;

    mutable std::mutex page_fault_handler_mutex_;
    mutable std::mutex backing_store_mutex_;
};