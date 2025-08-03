#include "MainMemory.h"
#include <sstream>
#include <iomanip>

MainMemory::MainMemory(int totalBytes, int frameSize)
    : totalMemoryBytes(totalBytes), frameSize(frameSize) {
    totalFrames = totalMemoryBytes / frameSize;
    frameTable.resize(totalFrames, "");
    validBits.resize(totalFrames, false);
}

// --- Private Unlocked Helpers ---

void MainMemory::_clearFrame_unlocked(int index) {
    if (index >= 0 && index < totalFrames) {
        frameTable[index].clear();
        validBits[index] = false;
    }
}

void MainMemory::_writeMemory_unlocked(const std::string& address, uint16_t value) {
    memory[address] = value;
}

uint16_t MainMemory::_readMemory_unlocked(const std::string& address) const {
    auto it = memory.find(address);
    return (it != memory.end()) ? it->second : 0;
}

int MainMemory::_getFreeFrameIndex_unlocked() const {
    for (int i = 0; i < totalFrames; ++i)
        if (!validBits[i]) return i;
    return -1;
}

int MainMemory::_getUsedFrames_unlocked() const {
    int used = 0;
    for (bool bit : validBits) {
        if (bit) {
            used++;
        }
    }
    return used;
}

// --- Public Locking Wrappers ---

int MainMemory::getTotalFrames() const {
    return totalFrames;
}

int MainMemory::getFreeFrameIndex() const {
    std::lock_guard<std::mutex> lock(memoryMutex_);
    return _getFreeFrameIndex_unlocked();
}

bool MainMemory::isFrameValid(int index) const {
    std::lock_guard<std::mutex> lock(memoryMutex_);
    return (index >= 0 && static_cast<size_t>(index) < validBits.size()) ? validBits[static_cast<size_t>(index)] : false;
}

void MainMemory::setFrame(int index, const std::string& pageId) {
    std::lock_guard<std::mutex> lock(memoryMutex_);
    if (index >= 0 && index < totalFrames) frameTable[index] = pageId;
}

void MainMemory::clearFrame(int index) {
    std::lock_guard<std::mutex> lock(memoryMutex_);
    _clearFrame_unlocked(index);
}

void MainMemory::markFrameValid(int index) {
    std::lock_guard<std::mutex> lock(memoryMutex_);
    if (index >= 0 && index < totalFrames) validBits[index] = true;
}

void MainMemory::markFrameInvalid(int index) {
    std::lock_guard<std::mutex> lock(memoryMutex_);
    if (index >= 0 && index < totalFrames) validBits[index] = false;
}

std::string MainMemory::getPageAtFrame(int index) const {
    std::lock_guard<std::mutex> lock(memoryMutex_);
    if (index >= 0 && index < totalFrames) return frameTable[index];
    return "";
}

void MainMemory::writeMemory(const std::string& address, uint16_t value) {
    std::lock_guard<std::mutex> lock(memoryMutex_);
    _writeMemory_unlocked(address, value);
}

uint16_t MainMemory::readMemory(const std::string& address) const {
    std::lock_guard<std::mutex> lock(memoryMutex_);
    return _readMemory_unlocked(address);
}

bool MainMemory::addressExists(const std::string& address) const {
    std::lock_guard<std::mutex> lock(memoryMutex_);
    return memory.count(address) > 0;
}

std::unordered_map<std::string, uint16_t>& MainMemory::getMemoryMap() {
    std::lock_guard<std::mutex> lock(memoryMutex_);
    return memory;
}

const std::vector<std::string>& MainMemory::getFrameTable() const {
    std::lock_guard<std::mutex> lock(memoryMutex_);
    return frameTable;
}

const std::vector<bool>& MainMemory::getValidBits() const {
    std::lock_guard<std::mutex> lock(memoryMutex_);
    return validBits;
}

std::vector<int> MainMemory::freeFramesByPagePrefix(const std::string& prefix) {
    std::lock_guard<std::mutex> lock(memoryMutex_);
    std::vector<int> freedFrames;
    for (size_t i = 0; i < frameTable.size(); ++i) {
        if (validBits[i] && frameTable[i].find(prefix) == 0) {
            _clearFrame_unlocked(static_cast<int>(i));
            freedFrames.push_back(static_cast<int>(i));
        }
    }
    return freedFrames;
}

std::vector<uint16_t> MainMemory::dumpPageFromFrame(int frameIndex, const std::string& baseAddress) {
    std::lock_guard<std::mutex> lock(memoryMutex_);
    int words_per_frame = frameSize / 2; // Calculate words
    std::vector<uint16_t> data;
    data.reserve(words_per_frame); // Reserve correct size
    int base = std::stoi(baseAddress, nullptr, 16);

    // Loop over words, not bytes, and calculate address correctly
    for (int i = 0; i < words_per_frame; ++i) {
        std::stringstream ss;
        // Physical addresses for words are 2 bytes apart
        ss << "0x" << std::hex << std::uppercase << (base + (i * 2));
        std::string addr = ss.str();
        data.push_back(_readMemory_unlocked(addr));
    }

    return data;
}

void MainMemory::loadPageToFrame(int frameIndex, const std::vector<uint16_t>& data, const std::string& baseAddress) {
    std::lock_guard<std::mutex> lock(memoryMutex_);
    int base = std::stoi(baseAddress, nullptr, 16);

    // Loop over the words in the data vector
    for (size_t i = 0; i < data.size(); ++i) {
        std::stringstream ss;
        // Physical addresses for words are 2 bytes apart
        ss << "0x" << std::hex << std::uppercase << (base + static_cast<int>(i * 2));
        std::string addr = ss.str();
        _writeMemory_unlocked(addr, data[i]);
    }
}

int MainMemory::getUsedFrames() const {
    std::lock_guard<std::mutex> lock(memoryMutex_);
    return _getUsedFrames_unlocked();
}

int MainMemory::getFreeFrames() const {
    std::lock_guard<std::mutex> lock(memoryMutex_);
    return _getFreeFrameIndex_unlocked() == -1 ? 0 : totalFrames - _getUsedFrames_unlocked();
}