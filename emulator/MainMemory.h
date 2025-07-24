#pragma once
#include <unordered_map>
#include <vector>
#include <string>
#include <cstdint>

class MainMemory {
public:
    MainMemory(int totalBytes, int frameSize);

    int getTotalFrames() const;
    int getFreeFrameIndex() const;
    bool isFrameValid(int frameIndex) const;
    void setFrame(int index, const std::string& pageId);
    void clearFrame(int index);
    void markFrameValid(int index);
    void markFrameInvalid(int index);
    std::string getPageAtFrame(int index) const;

    void writeMemory(const std::string& address, uint16_t value);
    uint16_t readMemory(const std::string& address) const;
    bool addressExists(const std::string& address) const;

    std::unordered_map<std::string, uint16_t>& getMemoryMap();
    const std::vector<std::string>& getFrameTable() const;
    const std::vector<bool>& getValidBits() const;

    void freeFramesByPagePrefix(const std::string& prefix);

private:
    int totalMemoryBytes;
    int frameSize;
    int totalFrames;

    std::unordered_map<std::string, uint16_t> memory; // hex address → value
    std::vector<std::string> frameTable;              // page ID per frame
    std::vector<bool> validBits;                      // valid/invalid per frame
};
