#include "MemoryManager.h"
#include "Process.h"
#include <sstream>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <random>
#include <stdexcept>

MemoryManager::MemoryManager(MainMemory& mem, int minMemProc, int maxMemProc, int frameSz)
    : memory(mem), minMemPerProc(minMemProc), maxMemPerProc(maxMemProc), frameSize(frameSz),
    pagedInCount(0), pagedOutCount(0), nextPageId(0) {
}

bool MemoryManager::allocateMemory(std::shared_ptr<Process> process, int requestedBytes) {
    process->setAllocatedMemory(requestedBytes);

    int pages = (requestedBytes + frameSize - 1) / frameSize;
    for (int i = 0; i < pages; ++i) {
        process->getPageTable()[i] = -1;
        process->getValidBits()[i] = false;
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
    memory.freeFramesByPagePrefix(prefix);
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

    // A page fault occurs if the page is not in the process's valid bit map, or if it is marked as not valid (not in memory).
    if (p->getValidBits().count(pageNum) == 0 || !p->getValidBits().at(pageNum)) {
        handlePageFault(p, pageNum);
    }

    int frameIndex = p->getPageTable().at(pageNum);
    return { frameIndex, offset };
}


// CHANGED: Dana - Implemented FIFO page replacement logic in handlePageFault
void MemoryManager::handlePageFault(std::shared_ptr<Process> p, int pageNum) {
    std::stringstream ss;
    ss << "p" << p->getPid() << "_page" << pageNum;
    std::string pageId = ss.str();

    int frameIndex = memory.getFreeFrameIndex();
    if (frameIndex == -1) {
        // If no free frames, get a victim using FIFO
        int victimFrame = getVictimFrame_FIFO();
        if (victimFrame != -1) {
            evictPage(victimFrame);
            frameIndex = victimFrame; // The newly freed frame is our target
        }
    }

    if (frameIndex != -1) {
        if (backingStore_.count(pageId)) {
            std::string baseAddr = "0x" + std::to_string((p->getPid() << 8) + pageNum * frameSize);
            memory.loadPageToFrame(frameIndex, backingStore_[pageId], baseAddr);
        }

        memory.setFrame(frameIndex, pageId);
        memory.markFrameValid(frameIndex);
        p->getPageTable()[pageNum] = frameIndex;
        p->getValidBits()[pageNum] = true;

        // Add the newly used frame to the back of the FIFO queue
        frame_fifo_queue_.push(frameIndex);

        ++pagedInCount;
    }
}

void MemoryManager::evictPage(int index) {
    std::string pageId = memory.getPageAtFrame(index);
    if (pageId.empty()) return;

    std::string baseAddr = "0x" + std::to_string(index * frameSize);
    std::vector<uint16_t> data = memory.dumpPageFromFrame(index, baseAddr);

    backingStore_[pageId] = data;
    writeToBackingStore(pageId);
    memory.clearFrame(index);
    ++pagedOutCount;
}

// CHANGED: Dana - Added helper function to get victim frame based on FIFO
int MemoryManager::getVictimFrame_FIFO() {
    if (frame_fifo_queue_.empty()) {
        return -1; // Should not happen if memory is full and all frames are tracked
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
