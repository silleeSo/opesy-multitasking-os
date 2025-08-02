#include "MemoryManager.h"
#include "Process.h"
#include "Scheduler.h"
#include <sstream>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <random>
#include <stdexcept>

MemoryManager::MemoryManager(MainMemory& mem, int minMemProc, int maxMemProc, int frameSz)
    : memory(mem), minMemPerProc(minMemProc), maxMemPerProc(maxMemProc), frameSize(frameSz),
    pagedInCount(0), pagedOutCount(0), nextPageId(0), scheduler_(nullptr) { // Initialize scheduler_ to nullptr
}

void MemoryManager::setScheduler(Scheduler* sched) {
    scheduler_ = sched;
}

bool MemoryManager::allocateMemory(std::shared_ptr<Process> process, int requestedBytes) {
    int pages_required = (requestedBytes + frameSize - 1) / frameSize;

    if (pages_required > memory.getFreeFrames()) {
        return false;
    }

    process->setAllocatedMemory(requestedBytes);

    // Lock the process's page table mutex before initializing it
    {
        std::lock_guard<std::mutex> lock(process->getPageTableMutex());
        for (int i = 0; i < pages_required; ++i) {
            process->getPageTable()[i] = -1;
            process->getValidBits()[i] = false;
        }
    }

    // Lock the backing store mutex to create the pages
    std::lock_guard<std::mutex> lock(backingStoreMutex_);
    for (int i = 0; i < pages_required; ++i) {
        std::stringstream ss;
        ss << "p" << process->getPid() << "_page" << i;
        std::string pageId = ss.str();
        std::vector<uint16_t> zeroPage(frameSize, 0);
        backingStore_[pageId] = zeroPage;
    }

    return true;
}

int MemoryManager::getRandomMemorySize() const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(minMemPerProc, maxMemPerProc);
    return dist(gen);
}

std::string MemoryManager::allocateVariable(std::shared_ptr<Process> process, const std::string& varName) {
    if (process->getSymbolTable().size() * 2 >= 64) {
        return "";
    }

    uint16_t base = static_cast<uint16_t>(process->getPid()) << 8;
    uint16_t offset = static_cast<uint16_t>(process->getSymbolTable().size() * 2);
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << base + offset;

    std::string address = ss.str();
    process->getSymbolTable()[varName] = address;
    write(address, 0, process);
    return address;
}

bool MemoryManager::isAddressInMemory(const std::string& addr) {
    return memory.addressExists(addr);
}

void MemoryManager::deallocate(uint64_t pid) {
    std::string prefix = "p" + std::to_string(pid) + "_page";

    // This now atomically frees frames and tells us which ones were freed.
    std::vector<int> freedFrames = memory.freeFramesByPagePrefix(prefix);
    if (freedFrames.empty()) {
        return; // Nothing was deallocated, so nothing to clean up.
    }

    // Use a set for fast lookups of the frames we need to remove.
    std::unordered_set<int> freedFramesSet(freedFrames.begin(), freedFrames.end());

    // Atomically rebuild the FIFO queue without the stale entries.
    std::lock_guard<std::mutex> lock(fifoQueueMutex_);
    std::queue<int> new_fifo_queue;
    while (!frame_fifo_queue_.empty()) {
        int frameIndex = frame_fifo_queue_.front();
        frame_fifo_queue_.pop();

        // Only keep frames that were NOT in the set of frames we just freed.
        if (freedFramesSet.find(frameIndex) == freedFramesSet.end()) {
            new_fifo_queue.push(frameIndex);
        }
    }
    frame_fifo_queue_ = std::move(new_fifo_queue);
}

uint16_t MemoryManager::read(const std::string& addr, std::shared_ptr<Process> p) {
    std::pair<int, int> result = translate(addr, p);
    int frame = result.first;
    int offset = result.second;
    int physicalAddr = frame * frameSize + offset;
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << physicalAddr;
    return memory.readMemory(ss.str());
}

void MemoryManager::write(const std::string& addr, uint16_t value, std::shared_ptr<Process> p) {
    std::pair<int, int> result = translate(addr, p);
    int frame = result.first;
    int offset = result.second;
    int physicalAddr = frame * frameSize + offset;
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << physicalAddr;
    memory.writeMemory(ss.str(), value);
}

std::pair<int, int> MemoryManager::translate(std::string logicalAddr, std::shared_ptr<Process> p) {
    int addr = 0;
    try {
        addr = std::stoi(logicalAddr, nullptr, 16);
    }
    catch (...) {
        p->setTerminationReason(Process::TerminationReason::MEMORY_VIOLATION, logicalAddr);
        throw std::runtime_error("Invalid memory address format.");
    }

    if (addr < 0 || addr >= p->getAllocatedMemory()) {
        p->setTerminationReason(Process::TerminationReason::MEMORY_VIOLATION, logicalAddr);
        throw std::runtime_error("Memory Access Violation");
    }

    int pageNum = addr / frameSize;
    int offset = addr % frameSize;
    bool needs_fault = false;

    // Lock, check the valid bit, then unlock
    {
        std::lock_guard<std::mutex> lock(p->getPageTableMutex());
        if (p->getValidBits().count(pageNum) == 0 || !p->getValidBits().at(pageNum)) {
            needs_fault = true;
        }
    }

    // If a fault is needed, handle it now while the lock is released
    if (needs_fault) {
        handlePageFault(p, pageNum);
    }

    // Re-lock to safely read the final frame index
    std::lock_guard<std::mutex> lock(p->getPageTableMutex());
    int frameIndex = p->getPageTable().at(pageNum);
    return { frameIndex, offset };
}

void MemoryManager::handlePageFault(std::shared_ptr<Process> p, int pageNum) {
    std::stringstream ss;
    ss << "p" << p->getPid() << "_page" << pageNum;
    std::string pageId = ss.str();

    int frameIndex = memory.getFreeFrameIndex();
    if (frameIndex == -1) {
        int victimFrame = getVictimFrame_FIFO();
        if (victimFrame != -1) {
            evictPage(victimFrame);
            frameIndex = victimFrame;
        }
    }

    if (frameIndex != -1) {
        std::string baseAddr = "0x" + std::to_string(frameIndex * frameSize);

        {
            std::lock_guard<std::mutex> lock(backingStoreMutex_);
            if (backingStore_.count(pageId)) {
                memory.loadPageToFrame(frameIndex, backingStore_[pageId], baseAddr);
            }
        }

        // Lock the page table before updating it
        {
            std::lock_guard<std::mutex> lock(p->getPageTableMutex());
            memory.setFrame(frameIndex, pageId);
            memory.markFrameValid(frameIndex);
            p->getPageTable()[pageNum] = frameIndex;
            p->getValidBits()[pageNum] = true;
        }

        {
            std::lock_guard<std::mutex> lock(fifoQueueMutex_);
            frame_fifo_queue_.push(frameIndex);
        }

        ++pagedInCount;
    }
}

void MemoryManager::preloadPages(std::shared_ptr<Process> process, int startPage, int numPages) {
    for (int i = 0; i < numPages; ++i) {
        int pageNum = startPage + i;
        if (process->getValidBits().count(pageNum) == 0 || !process->getValidBits().at(pageNum)) {
            handlePageFault(process, pageNum);
        }
    }
}

void MemoryManager::evictPage(int index) {
    std::string pageId = memory.getPageAtFrame(index);
    if (pageId.empty()) return;

    uint64_t ownerPid = -1;
    int pageNum = -1;
    size_t p_pos = pageId.find('p');
    size_t page_pos = pageId.find("_page");
    if (p_pos != std::string::npos && page_pos != std::string::npos) {
        ownerPid = std::stoull(pageId.substr(p_pos + 1, page_pos - (p_pos + 1)));
        pageNum = std::stoi(pageId.substr(page_pos + 5));
    }

    if (ownerPid != -1 && pageNum != -1 && scheduler_) {
        auto ownerProcess = scheduler_->findProcessById(ownerPid);
        if (ownerProcess) {
            // Lock the owner's mutex before modifying its page table
            std::lock_guard<std::mutex> lock(ownerProcess->getPageTableMutex());
            ownerProcess->getValidBits()[pageNum] = false;
        }
    }

    std::string baseAddr = "0x" + std::to_string(index * frameSize);
    std::vector<uint16_t> data = memory.dumpPageFromFrame(index, baseAddr);

    // Lock the backing store to write the evicted page data
    {
        std::lock_guard<std::mutex> lock(backingStoreMutex_);
        backingStore_[pageId] = data;
    }

    writeToBackingStore(pageId);
    memory.clearFrame(index);
    ++pagedOutCount;
}

int MemoryManager::getVictimFrame_FIFO() {
    std::lock_guard<std::mutex> lock(fifoQueueMutex_); // Lock the entire function

    if (frame_fifo_queue_.empty()) {
        return -1;
    }
    int victimFrame = frame_fifo_queue_.front();
    frame_fifo_queue_.pop();
    return victimFrame;
}

void MemoryManager::writeToBackingStore(const std::string& pageId) {
    std::ofstream out("csopesy-backing-store.txt", std::ios::app);
    out << "Evicted: " << pageId << std::endl;
}

void MemoryManager::logMemorySnapshot() {
    std::ofstream out("csopesy-vmstat.txt");
    out << "Frames: " << memory.getTotalFrames() << std::endl;
    out << "Paged In: " << pagedInCount << std::endl;
    out << "Paged Out: " << pagedOutCount << std::endl;
}

int MemoryManager::getPagedInCount() const { return pagedInCount; }
int MemoryManager::getPagedOutCount() const { return pagedOutCount; }
