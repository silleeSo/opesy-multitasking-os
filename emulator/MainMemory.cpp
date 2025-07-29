#include "MainMemory.h"
#include <sstream>
#include <iomanip>


MainMemory::MainMemory(int totalBytes, int frameSize)
    : totalMemoryBytes(totalBytes), frameSize(frameSize) {
    totalFrames = totalMemoryBytes / frameSize;
    frameTable.resize(totalFrames, "");
    validBits.resize(totalFrames, false);
}

int MainMemory::getTotalFrames() const { return totalFrames; }

int MainMemory::getFreeFrameIndex() const {
    for (int i = 0; i < totalFrames; ++i)
        if (!validBits[i]) return i;
    return -1;
}

bool MainMemory::isFrameValid(int index) const {
    return (index >= 0 && static_cast<size_t>(index) < validBits.size()) ? validBits[static_cast<size_t>(index)] : false;
}

void MainMemory::setFrame(int index, const std::string& pageId) {
    if (index >= 0 && index < totalFrames) frameTable[index] = pageId;
}

void MainMemory::clearFrame(int index) {
    if (index >= 0 && index < totalFrames) {
        frameTable[index].clear();
        validBits[index] = false;
    }
}

void MainMemory::markFrameValid(int index) {
    if (index >= 0 && index < totalFrames) validBits[index] = true;
}

void MainMemory::markFrameInvalid(int index) {
    if (index >= 0 && index < totalFrames) validBits[index] = false;
}

std::string MainMemory::getPageAtFrame(int index) const {
    if (index >= 0 && index < totalFrames) return frameTable[index];
    return "";
}

void MainMemory::writeMemory(const std::string& address, uint16_t value) {
    memory[address] = value;
}

uint16_t MainMemory::readMemory(const std::string& address) const {
    auto it = memory.find(address);
    return (it != memory.end()) ? it->second : 0;
}

bool MainMemory::addressExists(const std::string& address) const {
    return memory.count(address) > 0;
}

std::unordered_map<std::string, uint16_t>& MainMemory::getMemoryMap() {
    return memory;
}

const std::vector<std::string>& MainMemory::getFrameTable() const {
    return frameTable;
}

const std::vector<bool>& MainMemory::getValidBits() const {
    return validBits;
}

void MainMemory::freeFramesByPagePrefix(const std::string& prefix) {
    for (size_t i = 0; i < frameTable.size(); ++i) {
        if (validBits[i] && frameTable[i].find(prefix) == 0) {
            clearFrame(static_cast<int>(i));
        }
    }
}

std::vector<uint16_t> MainMemory::dumpPageFromFrame(int frameIndex, const std::string& baseAddress) {
    std::vector<uint16_t> data;
    int base = std::stoi(baseAddress, nullptr, 16);

    for (int i = 0; i < frameSize; ++i) {
        std::stringstream ss;
        ss << "0x" << std::hex << std::uppercase << (base + i);
        std::string addr = ss.str();

        data.push_back(readMemory(addr));
    }

    return data;
}

void MainMemory::loadPageToFrame(int frameIndex, const std::vector<uint16_t>& data, const std::string& baseAddress) {
    int base = std::stoi(baseAddress, nullptr, 16);
    for (size_t i = 0; i < data.size(); ++i) {
        std::stringstream ss;
        ss << "0x" << std::hex << std::uppercase << (base + static_cast<int>(i));
        std::string addr = ss.str();
        writeMemory(addr, data[i]);
    }
}

// CHANGED: Dana - Implemented getUsedFrames to calculate the number of occupied frames, supporting vmstat/process-smi.
int MainMemory::getUsedFrames() const {
    int used = 0;
    for (bool bit : validBits) {
        if (bit) {
            used++;
        }
    }
    return used;
}