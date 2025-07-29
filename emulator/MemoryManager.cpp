#include "MemoryManager.h"
#include "Process.h"
#include <sstream>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <random>
#include <stdexcept>
#include <cstdio>
#include <vector>

MemoryManager::MemoryManager(MainMemory& mem, int minMemProc, int maxMemProc, int frameSz)
    : memory(mem), minMemPerProc(minMemProc), maxMemPerProc(maxMemProc), frameSize(frameSz) {
    initializeBackingStoreFile();
}

void MemoryManager::initializeBackingStoreFile() {
    std::lock_guard<std::mutex> lock(backing_store_mutex_);
    std::ifstream file(backingStoreFileName);
    if (!file.good()) {
        std::ofstream out(backingStoreFileName);
        out << "# Backing Store for CSOPESY Emulator\n";
        out << "# Columns: Name        Type      Size(B) Dim   LogicalBaseAddress   PageData (space-separated)\n";
        out << "#---------------------------------------------------------------------------------------------\n";
    }
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

void MemoryManager::deallocate(uint64_t pid) {
    std::string pagePrefix = "proc" + std::to_string(pid) + "-";
    memory.freeFramesByPagePrefix(pagePrefix);

    // Remove any frame ownership entries for the deallocated process
    std::vector<int> frames_to_remove;
    // CORRECTED LOOP: Uses C++11 compatible syntax to avoid compiler errors.
    for (const auto& pair : frame_owners_) {
        const std::shared_ptr<Process>& owner = pair.second;
        if (owner->getPid() == pid) {
            frames_to_remove.push_back(pair.first);
        }
    }
    for (int frame_key : frames_to_remove) {
        frame_owners_.erase(frame_key);
    }

    std::lock_guard<std::mutex> lock(backing_store_mutex_);
    const std::string tempFileName = "backing-store.tmp";
    std::ifstream inFile(backingStoreFileName);
    std::ofstream outFile(tempFileName);

    if (!inFile.is_open() || !outFile.is_open()) {
        std::cerr << "Error: Could not open backing store files for deallocation." << std::endl;
        return;
    }

    std::string line;
    while (getline(inFile, line)) {
        if (line.rfind('#', 0) == 0 || line.find(pagePrefix) != 0) {
            outFile << line << std::endl;
        }
    }

    inFile.close();
    outFile.close();

    if (std::remove(backingStoreFileName.c_str()) != 0 && errno != ENOENT) {
        perror("Error deleting original backing store");
    }
    if (std::rename(tempFileName.c_str(), backingStoreFileName.c_str()) != 0) {
        perror("Error renaming temporary backing store");
    }
}

// DEFINITIVE FIX: This function is fully thread-safe. The original "double-checked locking"
// pattern was unsafe and caused a data race. This version locks before ANY access to the
// shared page table maps, which is the correct and safe approach.
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

    // Acquire lock BEFORE any access to the page table/valid bits to prevent data races.
    std::lock_guard<std::mutex> lock(page_fault_handler_mutex_);

    // Check for page fault while holding the lock.
    if (p->getValidBits().count(pageNum) == 0 || !p->getValidBits().at(pageNum)) {
        try {
            handlePageFault(p, pageNum);
        }
        catch (const std::runtime_error&) {
            p->setTerminationReason(Process::TerminationReason::MEMORY_VIOLATION, "Out of Memory");
            throw;
        }
    }

    // Access is now guaranteed to be safe.
    int frameIndex = p->getPageTable().at(pageNum);
    return { frameIndex, offset };
}


void MemoryManager::handlePageFault(std::shared_ptr<Process> p, int pageNum) {
    int frameIndex = memory.getFreeFrameIndex();
    if (frameIndex == -1) {
        int victimFrame = getVictimFrame_FIFO();
        if (victimFrame != -1) {
            evictPage(victimFrame);
            frameIndex = victimFrame;
        }
        else {
            throw std::runtime_error("Out of Memory: No free frames and no frames to evict.");
        }
    }

    std::stringstream ssPageId;
    ssPageId << "proc" << p->getPid() << "-page" << pageNum;
    std::string pageId = ssPageId.str();

    std::vector<uint16_t> pageData = loadPageFromFile(pageId);
    if (pageData.empty()) {
        pageData.assign(frameSize, 0);
    }

    std::stringstream ssBaseAddr;
    ssBaseAddr << "0x" << std::hex << (pageNum * frameSize);

    memory.loadPageToFrame(frameIndex, pageData, ssBaseAddr.str());
    memory.setFrame(frameIndex, pageId);
    memory.markFrameValid(frameIndex);
    p->getPageTable()[pageNum] = frameIndex;
    p->getValidBits()[pageNum] = true;

    // Track which process now owns this frame.
    frame_owners_[frameIndex] = p;

    frame_fifo_queue_.push(frameIndex);
    ++pagedInCount;
}

void MemoryManager::evictPage(int frameIndex) {
    std::string pageId = memory.getPageAtFrame(frameIndex);
    if (pageId.empty()) return;

    // Find the owner of the victim frame and invalidate its page table.
    if (frame_owners_.count(frameIndex)) {
        std::shared_ptr<Process> owner = frame_owners_.at(frameIndex);

        size_t pageNumPos = pageId.find("-page");
        if (pageNumPos != std::string::npos) {
            int pageNum = std::stoi(pageId.substr(pageNumPos + 5));
            if (owner->getValidBits().count(pageNum)) {
                owner->getValidBits()[pageNum] = false;
            }
        }
        frame_owners_.erase(frameIndex);
    }

    // Dump the page data to the backing store
    std::stringstream ssBaseAddr;
    size_t pageNumPos = pageId.find("-page");
    int pageNum = std::stoi(pageId.substr(pageNumPos + 5));
    ssBaseAddr << "0x" << std::hex << (pageNum * frameSize);
    std::string logicalBaseAddr = ssBaseAddr.str();
    std::vector<uint16_t> data = memory.dumpPageFromFrame(frameIndex, logicalBaseAddr);
    evictPageToFile(pageId, logicalBaseAddr, data);

    memory.clearFrame(frameIndex);
    ++pagedOutCount;
}

void MemoryManager::evictPageToFile(const std::string& pageId, const std::string& logicalBaseAddr, const std::vector<uint16_t>& pageData) {
    std::lock_guard<std::mutex> lock(backing_store_mutex_);
    std::ofstream outFile(backingStoreFileName, std::ios::app);
    if (!outFile) {
        std::cerr << "Error: Cannot open backing store file for writing." << std::endl;
        return;
    }

    outFile << std::left << std::setw(14) << pageId
        << std::setw(10) << "uint16_t"
        << std::setw(8) << (pageData.size() * sizeof(uint16_t))
        << std::setw(6) << 1
        << std::setw(21) << logicalBaseAddr;

    for (size_t i = 0; i < pageData.size(); ++i) {
        outFile << pageData[i] << (i == pageData.size() - 1 ? "" : " ");
    }
    outFile << std::endl;
}

std::vector<uint16_t> MemoryManager::loadPageFromFile(const std::string& pageId) {
    std::lock_guard<std::mutex> lock(backing_store_mutex_);
    std::ifstream inFile(backingStoreFileName);
    std::vector<uint16_t> pageData;
    std::string line;

    while (getline(inFile, line)) {
        if (line.rfind('#', 0) == 0) continue;

        std::stringstream ss(line);
        std::string name;
        ss >> name;

        if (name == pageId) {
            std::string type, size, dim, baseAddr;
            ss >> type >> size >> dim >> baseAddr;
            uint16_t value;
            while (ss >> value) {
                pageData.push_back(value);
            }
            break;
        }
    }
    return pageData;
}

int MemoryManager::getRandomMemorySize() const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(minMemPerProc, maxMemPerProc);
    return dist(gen);
}

std::string MemoryManager::allocateVariable(std::shared_ptr<Process> process, const std::string& varName) {
    if (process->getSymbolTable().size() * 2 >= 64) return "";
    uint16_t base = static_cast<uint16_t>(process->getPid()) << 8;
    uint16_t offset = static_cast<uint16_t>(process->getSymbolTable().size() * 2);
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << base + offset;
    std::string address = ss.str();
    process->getSymbolTable()[varName] = address;
    write(address, 0, process);
    return address;
}

bool MemoryManager::isAddressInMemory(const std::string& addr) { return memory.addressExists(addr); }
uint16_t MemoryManager::read(const std::string& addr, std::shared_ptr<Process> p) {
    std::pair<int, int> result = translate(addr, p);
    int physicalAddr = result.first * frameSize + result.second;
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << physicalAddr;
    return memory.readMemory(ss.str());
}
void MemoryManager::write(const std::string& addr, uint16_t value, std::shared_ptr<Process> p) {
    std::pair<int, int> result = translate(addr, p);
    int physicalAddr = result.first * frameSize + result.second;
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << physicalAddr;
    memory.writeMemory(ss.str(), value);
}
void MemoryManager::preloadPages(std::shared_ptr<Process> process, int startPage, int numPages) {
    for (int i = 0; i < numPages; ++i) {
        int pageNum = startPage + i;
        if (process->getValidBits().count(pageNum) == 0 || !process->getValidBits().at(pageNum)) {
            try { handlePageFault(process, pageNum); }
            catch (const std::runtime_error&) { break; }
        }
    }
}
int MemoryManager::getVictimFrame_FIFO() {
    if (frame_fifo_queue_.empty()) return -1;
    int victimFrame = frame_fifo_queue_.front();
    frame_fifo_queue_.pop();
    return victimFrame;
}
int MemoryManager::getPagedInCount() const { return pagedInCount; }
int MemoryManager::getPagedOutCount() const { return pagedOutCount; }