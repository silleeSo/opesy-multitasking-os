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
    pagedInCount(0), pagedOutCount(0), nextPageId(0) {}

bool MemoryManager::allocateMemory(std::shared_ptr<Process> process) {
    // Generate random memory size for the process between min and max
    int requestedBytes = getRandomMemorySize();
    int pages = (requestedBytes + frameSize - 1) / frameSize;

    for (int i = 0; i < pages; ++i) {
        int freeIndex = memory.getFreeFrameIndex();
        std::stringstream ss;
        ss << process->getName() << "_page" << i;
        std::string pageId = ss.str();

        if (freeIndex == -1) {
            // No available frame, evict one
            evictPage(0); // For now, use first-fit eviction
            freeIndex = memory.getFreeFrameIndex();
        }

        memory.setFrame(freeIndex, pageId);
        memory.markFrameValid(freeIndex);

        process->getPageTable()[i] = freeIndex;
        process->getValidBits()[i] = true;

        ++pagedInCount;
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
    if (process->getSymbolTable().size() >= 32) return "";

    uint16_t base = static_cast<uint16_t>(process->getPid()) << 8;
    uint16_t offset = static_cast<uint16_t>(process->getSymbolTable().size() * 2);
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << base + offset;

    std::string address = ss.str();
    process->getSymbolTable()[varName] = address;
    memory.writeMemory(address, 0);
    return address;
}

bool MemoryManager::isAddressInMemory(const std::string& addr) {
    return memory.addressExists(addr);
}

void MemoryManager::deallocate(uint64_t  pid) {
    // Assumes page IDs were named like: "p42_page0", "p42_page1", etc.
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
    int addr = std::stoi(logicalAddr, nullptr, 16);
    int pageNum = addr / frameSize;
    int offset = addr % frameSize;

    if (p->getValidBits().count(pageNum) == 0 || !p->getValidBits()[pageNum]) {
        throw std::runtime_error("Page fault at logical address " + logicalAddr);
    }

    int frameIndex = p->getPageTable().at(pageNum);
    return { frameIndex, offset };
}

void MemoryManager::evictPage(int index) {
    std::string pageId = memory.getPageAtFrame(index);
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
