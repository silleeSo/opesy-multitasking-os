#include "MemoryManager.h"
#include "Process.h"
#include <sstream>
#include <iostream>
#include <iomanip>
#include <filesystem>

MemoryManager::MemoryManager(MainMemory& mem, int frameSz)
    : memory(mem), frameSize(frameSz), nextPageId(0) {}

bool MemoryManager::allocateMemory(std::shared_ptr<Process> process, int requestedBytes) {
    if (requestedBytes < 64) return false;

    int pages = (requestedBytes + frameSize - 1) / frameSize;
    for (int i = 0; i < pages; ++i) {
        int freeIndex = memory.getFreeFrameIndex();
        std::stringstream ss;
        ss << process->getName() << "_page" << i;

        std::string pageId = ss.str();
        if (freeIndex == -1) {
            // Evict page
            evictPage(0); // Simple: evict first
            freeIndex = memory.getFreeFrameIndex();
        }

        memory.setFrame(freeIndex, pageId);
        memory.markFrameValid(freeIndex);
        ++pagedInCount;
    }
    return true;
}

std::string MemoryManager::allocateVariable(std::shared_ptr<Process> process, const std::string& varName) {
    if (process->getSymbolTable().size() >= 32) return "";

    // Allocate next free memory address (simulate)
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
std::pair<int, int> MemoryManager::translate(std::string logicalAddr, std::shared_ptr<Process> p) {
    int addr = std::stoi(logicalAddr, nullptr, 16);
    int pageNum = addr / frameSize;
    int offset = addr % frameSize;

    if (p->getValidBits().count(pageNum) == 0 || !p->getValidBits()[pageNum]) {
        // Simulate page fault
        throw std::runtime_error("Page fault at logical address " + logicalAddr);
    }

    int frameIndex = p->getPageTable()[pageNum];
    return { frameIndex, offset };
}

int MemoryManager::getPagedInCount() const { return pagedInCount; }
int MemoryManager::getPagedOutCount() const { return pagedOutCount; }
