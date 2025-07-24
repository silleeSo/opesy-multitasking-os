#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>

//NEWLY ADDED: Chrysille
#include <unordered_map>
#include <string>
#include <memory>
//

class Process {
public:
    struct Instruction {
        uint8_t opcode = 0;
        std::vector<std::string> args;
    };

    struct LoopState {
        size_t startIns;
        uint16_t repeats;
    };

    Process(uint64_t pid, std::string name);
    uint64_t getPid() const { return pid_; }
    const std::string& getName() const { return name_; }
    bool isFinished() const { return finished_; }
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

    const std::unordered_map<std::string, uint16_t>& getVariables() const { return vars; }

    std::string smi() const;
    void execute(const Instruction& ins, int coreId = -1);
    void genRandInst(uint64_t min_ins, uint64_t max_ins);
    bool runOneInstruction(int coreId = -1);
    void setIsSleeping(bool val, uint64_t targetTick = 0) {
        isSleeping_ = val;
        sleepTargetTick_ = targetTick;
    }

    //NEWLY ADDED: Chrysille
    std::unordered_map<std::string, std::string>& getSymbolTable() {
        return symbolTable_;
    }

    std::unordered_map<int, int>& getPageTable() {
        return pageTable_;
    }

    std::unordered_map<int, bool>& getValidBits() {
        return validBits_;
    }


private:
    uint64_t pid_;
    std::string name_;
    bool finished_;
    bool isSleeping_ = false;
    uint64_t sleepTargetTick_ = 0;
    int lastCoreId_ = -1;  // -1 means unassigned or unknown
    time_t finishTime_ = 0;
    std::vector<Instruction> insList;
    size_t insCount_ = 0;
    std::unordered_map<std::string, uint16_t> vars;
    std::vector<LoopState> loopStack;
    std::vector<std::pair<time_t, std::string>> logs_;

    //NEWLY ADDED: Chrysille
    std::unordered_map<std::string, std::string> symbolTable_;  // var name → logical addr
    std::unordered_map<int, int> pageTable_;                    // logical page → physical frame
    std::unordered_map<int, bool> validBits_;                   // page validity
    class MemoryManager* memoryManager = nullptr;


};