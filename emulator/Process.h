#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>
#include <ctime> 

class MemoryManager;
class Process;

class Process : public std::enable_shared_from_this<Process> {
public:
    enum class TerminationReason { RUNNING, FINISHED_NORMALLY, MEMORY_VIOLATION };

    struct Instruction {
        uint8_t opcode = 0;
        std::vector<std::string> args;
    };

    struct LoopState {
        size_t startIns;
        uint16_t repeats;
    };

    Process(uint64_t pid, std::string name, MemoryManager* memManager);

    uint64_t getPid() const { return pid_; }
    const std::string& getName() const { return name_; }

    bool isFinished() const {
        return finished_ || (terminationReason_ != TerminationReason::RUNNING);
    }
    bool isSleeping() const { return isSleeping_; }
    uint64_t getSleepTargetTick() const { return sleepTargetTick_; }
    size_t getCurrentInstructionIndex() const { return insCount_; }
    size_t getTotalInstructions() const { return insList.size(); }
    const std::vector<std::pair<time_t, std::string>>& getLogs() const {
        return logs_;
    }

    void setLastCoreId(int id) { lastCoreId_ = id; }
    int getLastCoreId() const { return lastCoreId_; }

    void setFinishTime(time_t t) { finishTime_ = t; }
    time_t getFinishTime() const { return finishTime_; }

    std::string smi() const;
    void execute(const Instruction& ins, int coreId = -1);
    void genRandInst(uint64_t min_ins, uint64_t max_ins);
    // CHANGED: Dana - Added a new method to load instructions from a string for screen -c
    void loadInstructionsFromString(const std::string& instruction_str);
    bool runOneInstruction(int coreId = -1);
    void setIsSleeping(bool val, uint64_t targetTick = 0) {
        isSleeping_ = val;
        sleepTargetTick_ = targetTick;
    }

    std::unordered_map<std::string, std::string>& getSymbolTable() {
        return symbolTable_;
    }

    std::unordered_map<int, int>& getPageTable() {
        return pageTable_;
    }

    std::unordered_map<int, bool>& getValidBits() {
        return validBits_;
    }

    void setAllocatedMemory(int bytes) { allocatedMemoryBytes_ = bytes; }
    int getAllocatedMemory() const { return allocatedMemoryBytes_; }

    void setTerminationReason(TerminationReason reason, const std::string& addr = "") {
        if (terminationReason_ == TerminationReason::RUNNING) {
            terminationReason_ = reason;
            if (reason == TerminationReason::MEMORY_VIOLATION) {
                violationTime_ = time(nullptr);
                violationAddress_ = addr;
            }
            if (reason != TerminationReason::RUNNING) {
                finished_ = true;
            }
        }
    }
    TerminationReason getTerminationReason() const { return terminationReason_; }
    time_t getViolationTime() const { return violationTime_; }
    std::string getViolationAddress() const { return violationAddress_; }


private:
    uint64_t pid_;
    std::string name_;
    bool finished_;
    bool isSleeping_ = false;
    uint64_t sleepTargetTick_ = 0;
    int lastCoreId_ = -1;
    time_t finishTime_ = 0;
    std::vector<Instruction> insList;
    size_t insCount_ = 0;
    std::vector<LoopState> loopStack;
    std::vector<std::pair<time_t, std::string>> logs_;
    std::unordered_map<std::string, std::string> symbolTable_;
    size_t symbolTableOffset_ = 0;
    std::unordered_map<int, int> pageTable_;
    std::unordered_map<int, bool> validBits_;
    MemoryManager* memoryManager_ = nullptr;

    int allocatedMemoryBytes_ = 0;
    TerminationReason terminationReason_ = TerminationReason::RUNNING;
    time_t violationTime_ = 0;
    std::string violationAddress_ = "";
};
