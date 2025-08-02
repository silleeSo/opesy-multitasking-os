#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <memory> // For std::enable_shared_from_this
#include <unordered_map>
#include <utility>
#include <ctime>
#include <mutex>

// Forward declaration to avoid circular dependency
class MemoryManager;

// Represents a single instruction for a process
struct Instruction {
    uint8_t opcode = 0;
    std::vector<std::string> args;
};

// Represents a state for a FOR loop
struct LoopState {
    uint64_t startIns;
    uint16_t repeats;
};

// The Process class now correctly inherits from std::enable_shared_from_this
class Process : public std::enable_shared_from_this<Process> {
public:
    enum class TerminationReason {
        RUNNING,
        FINISHED_NORMALLY,
        MEMORY_VIOLATION
    };

    // Constructor
    Process(uint64_t pid, std::string name, MemoryManager* memManager);

    // Public Methods
    void execute(const Instruction& ins, int coreId);
    bool runOneInstruction(int coreId);
    void genRandInst(uint64_t min_ins, uint64_t max_ins);
    void loadInstructionsFromString(const std::string& instruction_str);
    std::string smi() const;

    // Getters
    uint64_t getPid() const { return pid_; }
    const std::string& getName() const { return name_; }
    bool isFinished() const { return finished_.load(); }
    bool isSleeping() const { return isSleeping_.load(); }
    uint64_t getSleepTargetTick() const { return sleepTargetTick_; }
    size_t getTotalInstructions() const { return insList.size(); }
    uint64_t getCurrentInstructionIndex() const { return insCount_; }
    time_t getFinishTime() const { return finishTime_; }
    int getAllocatedMemory() const { return allocatedMemoryBytes_; }
    bool hasBeenScheduled() const { return hasBeenScheduled_; }
    TerminationReason getTerminationReason() const { return terminationReason_; }
    time_t getViolationTime() const { return violationTime_; }
    const std::string& getViolationAddress() const { return violationAddress_; }
    std::unordered_map<int, int>& getPageTable() { return pageTable_; }
    const std::unordered_map<int, int>& getPageTable() const { return pageTable_; }
    std::unordered_map<int, bool>& getValidBits() { return validBits_; }
    const std::unordered_map<int, bool>& getValidBits() const { return validBits_; }
    std::unordered_map<std::string, std::string>& getSymbolTable() { return symbolTable_; }
    int getSymbolTablePages(int frameSize) const;


    // Setters
    void setLastCoreId(int id) { lastCoreId_ = id; }
    void setIsSleeping(bool sleeping) { isSleeping_ = sleeping; }
    void setFinishTime(time_t t) { finishTime_ = t; }
    void setAllocatedMemory(int bytes) { allocatedMemoryBytes_ = bytes; }
    void setHasBeenScheduled(bool scheduled) { hasBeenScheduled_ = scheduled; }
    void setTerminationReason(TerminationReason reason, const std::string& addr = "");


private:
    uint64_t pid_;
    std::string name_;
    std::atomic<bool> finished_;
    std::atomic<bool> isSleeping_;
    uint64_t sleepTargetTick_;
    time_t finishTime_{ 0 };
    int lastCoreId_{ -1 };

    // Instructions
    std::vector<Instruction> insList;
    uint64_t insCount_{ 0 };
    std::vector<LoopState> loopStack;

    // Logs
    std::vector<std::pair<time_t, std::string>> logs_;
    mutable std::mutex logsMutex_; // Add this mutex

    // Memory
    MemoryManager* memoryManager_;
    int allocatedMemoryBytes_;
    std::unordered_map<std::string, std::string> symbolTable_;
    int symbolTableOffset_{ 0 };
    std::unordered_map<int, int> pageTable_;
    std::unordered_map<int, bool> validBits_;
    bool hasBeenScheduled_;

    // Termination info
    TerminationReason terminationReason_;
    time_t violationTime_;
    std::string violationAddress_;
};