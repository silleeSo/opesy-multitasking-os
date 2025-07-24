#include "MainMemory.h"

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
    return (index >= 0 && index < validBits.size()) ? validBits[index] : false;
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
