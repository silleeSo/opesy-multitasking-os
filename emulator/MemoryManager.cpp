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

    // --- REMOVE THIS BLOCK (Admission Control for Physical Frames) ---
    // if (pages_required > memory.getFreeFrames()) {
    //     return false;
    // }
    // --- END REMOVAL ---

    process->setAllocatedMemory(requestedBytes);

    {
        std::lock_guard<std::mutex> lock(process->getPageTableMutex());
        for (int i = 0; i < pages_required; ++i) {
            process->getPageTable()[i] = -1;
            process->getValidBits()[i] = false; // Initially all pages are in backing store
        }
    }

    // Lock the backing store mutex to create the pages (conceptually)
    {
        std::lock_guard<std::mutex> lock(backingStoreMutex_);
        for (int i = 0; i < pages_required; ++i) {
            std::stringstream ss;
            ss << "p" << process->getPid() << "_page" << i;
            std::string pageId = ss.str();
            std::vector<uint16_t> zeroPage(frameSize / 2, 0);
            backingStore_[pageId] = zeroPage; // Pages are "stored" in the backingStore_ map by default
        }
    }

    return true; // Always succeed if virtual allocation and backing store setup is complete
}

int MemoryManager::getRandomMemorySize() const {
    // 1. Create a list of all valid power-of-2 sizes in the configured range.
    std::vector<int> powerOfTwoSizes;
    for (int size = minMemPerProc; size <= maxMemPerProc; size *= 2) {
        if (size > 0) { // Ensure we don't loop infinitely if minMemPerProc is 0
            powerOfTwoSizes.push_back(size);
        }
    }

    if (powerOfTwoSizes.empty()) {
        // Fallback to min size if the range is invalid (e.g., min > max)
        return minMemPerProc;
    }

    // 2. Pick a random INDEX from the list of valid sizes.
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, static_cast<int>(powerOfTwoSizes.size()) - 1);

    // 3. Return the valid power-of-2 size at that index.
    return powerOfTwoSizes[dist(gen)];
}

std::string MemoryManager::allocateVariable(std::shared_ptr<Process> process, const std::string& varName) {
    // Check if symbol table is full (max 32 variables * 2 bytes/variable = 64 bytes)
    if (process->getSymbolTable().size() * 2 >= 64) { // 
        return ""; // Cannot allocate more variables 
    }

    // The logical address for a variable starts from 0x0 within the process's memory space.
    // The offset is based on the current size of the symbol table * 2 (for uint16_t).
    uint16_t currentOffsetInSymbolTable = static_cast<uint16_t>(process->getSymbolTable().size() * 2);

    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << currentOffsetInSymbolTable; // Logical address is just the offset within the symbol table segment

    std::string address = ss.str();
    process->getSymbolTable()[varName] = address;
    // When a variable is declared, its initial value is 0 unless specified.
    // The problem implies initial value 0 for uninitialized blocks, but for DECLARE with a value, it's set.
    // For allocateVariable, which is called by DECLARE without an explicit value, 0 is appropriate.
    write(address, 0, process); // Write 0 to the newly allocated variable's memory location
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

uint16_t MemoryManager::read(const std::string& logicalAddr, std::shared_ptr<Process> p) {
    // Translate the logical address to a physical frame and offset.
    std::pair<int, int> physicalLocation = translate(logicalAddr, p);
    int frameIndex = physicalLocation.first;
    int offset = physicalLocation.second;

    // The physical address is the base of the frame plus the offset.
    int physicalByteAddress = frameIndex * frameSize + offset;

    // Convert the physical byte address to the hex string key used by MainMemory.
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << physicalByteAddress;

    return memory.readMemory(ss.str());
}

void MemoryManager::write(const std::string& logicalAddr, uint16_t value, std::shared_ptr<Process> p) {
    // Translate the logical address to a physical frame and offset.
    std::pair<int, int> physicalLocation = translate(logicalAddr, p);
    int frameIndex = physicalLocation.first;
    int offset = physicalLocation.second;

    // The physical address is the base of the frame plus the offset.
    int physicalByteAddress = frameIndex * frameSize + offset;

    // Convert the physical byte address to the hex string key used by MainMemory.
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << physicalByteAddress;

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

    if (addr < 0 || (addr + 1) >= p->getAllocatedMemory()) {
        p->setTerminationReason(Process::TerminationReason::MEMORY_VIOLATION, logicalAddr);
        throw std::runtime_error("Memory Access Violation");
    }

    int pageNum = addr / frameSize;
    int offset = addr % frameSize;
    bool needs_fault = false;

    {
        std::lock_guard<std::mutex> lock(p->getPageTableMutex());
        if (p->getValidBits().count(pageNum) == 0 || !p->getValidBits().at(pageNum)) {
            needs_fault = true;
        }
    }

    if (needs_fault) {
        handlePageFault(p, pageNum);
    }

    std::lock_guard<std::mutex> lock(p->getPageTableMutex());
    int frameIndex = p->getPageTable().at(pageNum);
    return { frameIndex, offset };
}

void MemoryManager::handlePageFault(std::shared_ptr<Process> p, int pageNum) {
    std::stringstream ss_pageId;
    ss_pageId << "p" << p->getPid() << "_page" << pageNum;
    std::string pageId = ss_pageId.str();

    int frameIndex = memory.getFreeFrameIndex();
    if (frameIndex == -1) {
        int victimFrame = getVictimFrame_FIFO();
        if (victimFrame != -1) {
            evictPage(victimFrame);
            frameIndex = victimFrame;
        }
    }

    if (frameIndex != -1) {
        // --- FIX: Use a stringstream to correctly format the base address as hexadecimal ---
        std::stringstream ss_baseAddr;
        ss_baseAddr << "0x" << std::hex << (frameIndex * frameSize);
        std::string baseAddr = ss_baseAddr.str();

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
    std::shared_ptr<Process> ownerProcess = nullptr; // <--- Added
    size_t p_pos = pageId.find('p');
    size_t page_pos = pageId.find("_page");
    if (p_pos != std::string::npos && page_pos != std::string::npos) {
        ownerPid = std::stoull(pageId.substr(p_pos + 1, page_pos - (p_pos + 1)));
        pageNum = std::stoi(pageId.substr(page_pos + 5));
    }

    if (ownerPid != -1 && pageNum != -1 && scheduler_) {
        ownerProcess = scheduler_->findProcessById(ownerPid); // <--- Get owner process
        if (ownerProcess) {
            std::lock_guard<std::mutex> lock(ownerProcess->getPageTableMutex());
            ownerProcess->getValidBits()[pageNum] = false;
        }
    }

    std::stringstream ss_baseAddr;
    ss_baseAddr << "0x" << std::hex << (index * frameSize);
    std::string baseAddr = ss_baseAddr.str();

    std::vector<uint16_t> data = memory.dumpPageFromFrame(index, baseAddr);

    {
        std::lock_guard<std::mutex> lock(backingStoreMutex_);
        backingStore_[pageId] = data;
    }

    // Pass all relevant information to the logging function
    writeToBackingStore(pageId, ownerProcess, index, data); // <--- Modified call
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

void MemoryManager::writeToBackingStore(const std::string& pageId, std::shared_ptr<Process> ownerProcess, int frameIndex, const std::vector<uint16_t>& pageData) {
    std::ofstream out("csopesy-backing-store.txt", std::ios::app);
    if (!out.is_open()) {
        std::cerr << "Error: Could not open csopesy-backing-store.txt for writing." << std::endl;
        return;
    }

    time_t now = time(nullptr);
    tm localtm{};
#ifdef _WIN32
    localtime_s(&localtm, &now);
#else
    localtime_r(&now, &localtm);
#endif
    char buf[64];
    strftime(buf, sizeof(buf), "%m/%d/%Y %I:%M:%S %p", &localtm);

    out << "\n+==========================================================================+\n";
    std::string title = "BACKING STORE SNAPSHOT - " + std::string(buf);
    int totalWidth = 74;
    int padding = (totalWidth - static_cast<int>(title.length())) / 2;
    out << "|" << std::string(padding, ' ') << title << std::string(totalWidth - padding - title.length(), ' ') << "|\n";
    out << "+==========================================================================+\n\n";

    uint64_t ownerPid = -1;
    int pageNum = -1;
    size_t p_pos = pageId.find('p');
    size_t page_pos = pageId.find("_page");
    if (p_pos != std::string::npos && page_pos != std::string::npos) {
        ownerPid = std::stoull(pageId.substr(p_pos + 1, page_pos - (p_pos + 1)));
        pageNum = std::stoi(pageId.substr(page_pos + 5));
    }

    out << "Evicted Page        : " << pageId << "\n";
    if (ownerProcess) {
        out << "Owner Process       : " << ownerProcess->getName() << " (PID: " << ownerProcess->getPid() << ")\n";
    }
    else {
        out << "Owner Process       : Unknown (PID: " << ownerPid << ")\n";
    }
    out << "Logical Page Number : " << pageNum << "\n";
    out << "Evicted From Frame  : " << frameIndex << "\n\n";

    // --- Page Data Table ---
    out << "+----------------------------- Page Data (Hex) -----------------------------+\n";
    out << "| Offset | Value  | Offset | Value  | Offset | Value  | Offset | Value     |\n";
    out << "+--------+--------+--------+--------+--------+--------+--------+-----------+\n";

    out << std::hex << std::uppercase << std::setfill('0');
    for (size_t i = 0; i < pageData.size(); i += 4) {
        for (int j = 0; j < 4; ++j) {
            size_t idx = i + j;
            if (idx < pageData.size()) {
                int logicalOffset = static_cast<int>(pageNum * frameSize + idx * 2);
                out << "| 0x" << std::setw(2) << logicalOffset
                    << " | 0x" << std::setw(4) << pageData[idx] << " ";
            }
            else {
                out << "|        |        ";
            }
        }
        out << "|\n";
    }
    out << "+-------------------------------------------------------------------------+\n";

    // --- Symbol Table ---
    if (pageNum == 0 && ownerProcess) {
        out << "\nSymbol Table (Page 0):\n";
        out << "+----------+--------------+--------+\n";
        out << "| Variable | Logical Addr | Value  |\n";
        out << "+----------+--------------+--------+\n";

        std::lock_guard<std::mutex> lock(ownerProcess->getPageTableMutex());
        const auto& symbolTable = ownerProcess->getSymbolTable();
        for (const auto& pair : symbolTable) {
            const std::string& varName = pair.first;
            const std::string& logicalAddr = pair.second;
            int varOffset = std::stoi(logicalAddr, nullptr, 16);
            uint16_t varValue = 0;
            if (varOffset >= 0 && varOffset / 2 < static_cast<int>(pageData.size())) {
                varValue = pageData[varOffset / 2];
            }

            out << "| " << std::left << std::setw(8) << varName
                << "| " << std::right << std::setw(12) << logicalAddr
                << " | 0x" << std::setw(4) << varValue << " |\n";
        }
        out << "+----------+--------------+--------+\n";
    }

    out << "===========================================================================\n";
}


void MemoryManager::logMemorySnapshot() {
    std::ofstream out("csopesy-vmstat.txt");
    out << "Frames: " << memory.getTotalFrames() << std::endl;
    out << "Paged In: " << pagedInCount << std::endl;
    out << "Paged Out: " << pagedOutCount << std::endl;
}

int MemoryManager::getPagedInCount() const { return pagedInCount; }
int MemoryManager::getPagedOutCount() const { return pagedOutCount; }
