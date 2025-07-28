#pragma once
#include <unordered_map>
#include <vector>
#include <string>
#include <cstdint>
#include <mutex>

class MainMemory {
public:
    MainMemory(int totalBytes, int frameSize);

    // Public thread-safe methods
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

    std::vector<uint16_t> dumpPageFromFrame(int frameIndex, const std::string& baseAddress);
    void loadPageToFrame(int frameIndex, const std::vector<uint16_t>& data, const std::string& baseAddress);

    int getUsedFrames() const;
    int getFreeFrames() const;
    int getTotalMemoryBytes() const;
    int getFrameSize() const;

private:
    // Private helper methods that do not lock the mutex themselves.
    // They are called by public methods that HAVE already acquired the lock.
    void _clearFrame_unlocked(int index);
    void _writeMemory_unlocked(const std::string& address, uint16_t value);
    uint16_t _readMemory_unlocked(const std::string& address) const;
    int _getFreeFrameIndex_unlocked() const;
    int _getUsedFrames_unlocked() const;

    int totalMemoryBytes;
    int frameSize;
    int totalFrames;

    // Shared data structures
    std::unordered_map<std::string, uint16_t> memory;
    std::vector<std::string> frameTable;
    std::vector<bool> validBits;

    // Mutex to protect shared data.
    // 'mutable' allows the mutex to be locked even in const member functions.
    mutable std::mutex memoryMutex_;
};