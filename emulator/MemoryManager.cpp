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

bool MemoryManager::allocateMemory(std::shared_ptr<Process> process) {
    int requestedBytes = getRandomMemorySize();
    // CHANGED: Dana - Store the allocated memory size directly in the process object
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
    if (process->getSymbolTable().size() * 2 >= maxMemPerProc) return "";

    uint16_t base = static_cast<uint16_t>(process->getPid()) << 8;
    uint16_t offset = static_cast<uint16_t>(process->getSymbolTable().size() * 2);
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << base + offset;

    std::string address = ss.str();
    process->getSymbolTable()[varName] = address;
    write(address, 0, process);
    return address;
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

// CHANGED: Dana - Added comprehensive Memory Access Violation (MAV) check to translate()
std::pair<int, int> MemoryManager::translate(std::string logicalAddr, std::shared_ptr<Process> p) {
    int addr = 0;
    try {
        addr = std::stoi(logicalAddr, nullptr, 16);
    }
    catch (...) {
        p->setTerminationReason(Process::TerminationReason::MEMORY_VIOLATION, logicalAddr);
        throw std::runtime_error("Invalid memory address format.");
    }

    // This is the core Memory Access Violation check.
    if (addr < 0 || addr >= p->getAllocatedMemory()) {
        p->setTerminationReason(Process::TerminationReason::MEMORY_VIOLATION, logicalAddr);
        throw std::runtime_error("Memory Access Violation");
    }

    int pageNum = addr / frameSize;
    int offset = addr % frameSize;

    if (p->getValidBits().count(pageNum) == 0 || !p->getValidBits()[pageNum]) {
        handlePageFault(p, pageNum);
    }

    int frameIndex = p->getPageTable().at(pageNum);
    return { frameIndex, offset };
}


void MemoryManager::handlePageFault(std::shared_ptr<Process> p, int pageNum) {
    std::stringstream ss;
    ss << "p" << p->getPid() << "_page" << pageNum;
    std::string pageId = ss.str();

    int frameIndex = memory.getFreeFrameIndex();
    if (frameIndex == -1) {
        evictPage(0); // Placeholder for a real replacement algorithm
        frameIndex = memory.getFreeFrameIndex();
    }

    if (backingStore_.count(pageId)) {
        std::string baseAddr = "0x" + std::to_string((p->getPid() << 8) + pageNum * frameSize);
        memory.loadPageToFrame(frameIndex, backingStore_[pageId], baseAddr);
    }

    memory.setFrame(frameIndex, pageId);
    memory.markFrameValid(frameIndex);
    p->getPageTable()[pageNum] = frameIndex;
    p->getValidBits()[pageNum] = true;
    ++pagedInCount;
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
