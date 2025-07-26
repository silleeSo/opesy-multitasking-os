#include <iostream>
#include <thread>
#include <chrono>
#include <climits>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <string>
#include <random>
#include <vector>
#include <sstream>
#include <ctime>
#include <iomanip>
#include <algorithm>

#include "Process.h"
#include "GlobalState.h"
#include "MemoryManager.h" 

static std::random_device rd;
static std::mt19937 gen(rd());

Process::Process(uint64_t pid, std::string name, MemoryManager* memManager)
    : pid_(pid), name_(std::move(name)), finished_(false), isSleeping_(false), sleepTargetTick_(0), memoryManager_(memManager) {
}

void Process::execute(const Instruction& ins, int coreId) {
    // CHANGED: Dana - The getValue lambda now reads from memory via the MemoryManager.
    auto getValue = [this](const std::string& token) -> uint16_t {
        // Check if the token is a literal number
        if (isdigit(token[0]) || (token[0] == '-' && token.size() > 1)) {
            try {
                return static_cast<uint16_t>(std::stoi(token));
            }
            catch (const std::out_of_range&) {
                return 0;
            }
        }
        // Otherwise, it's a variable name. Look it up in the symbol table.
        else {
            if (symbolTable_.count(token)) {
                const std::string& address = symbolTable_.at(token);
                if (memoryManager_) {
                    // Pass a temporary shared_ptr to this. A better solution would be to pass the process's shared_ptr down.
                    return memoryManager_->read(address, std::shared_ptr<Process>(this, [](Process*) {}));
                }
                return 0; // Return 0 if memory manager is not available
            }
            return 0; // Return 0 if variable not found
        }
        };

    auto clamp = [](int64_t val) -> uint16_t {
        if (val < 0) return 0;
        if (val > UINT16_MAX) return UINT16_MAX;
        return static_cast<uint16_t>(val);
        };

    // CHANGED: Dana - DECLARE now allocates a logical address and writes the initial value to memory.
    if (ins.opcode == 1 && ins.args.size() >= 1) { // DECLARE
        // Enforce 32-variable limit
        if (symbolTable_.size() >= 32) {
            logs_.emplace_back(time(nullptr), "[Warning] Symbol table full. DECLARE ignored.");
            return;
        }

        const std::string& varName = ins.args[0];

        // Calculate the logical address for the new variable
        std::stringstream ss;
        ss << "0x" << std::hex << std::setw(4) << std::setfill('0') << symbolTableOffset_;
        std::string logicalAddress = ss.str();

        // Map the variable name to its new address
        symbolTable_[varName] = logicalAddress;
        symbolTableOffset_ += 2; // Each variable is 2 bytes

        // If an initial value is provided, write it to the new address
        if (ins.args.size() == 2) {
            uint16_t initialValue = clamp(getValue(ins.args[1]));
            if (memoryManager_) {
                memoryManager_->write(logicalAddress, initialValue, std::shared_ptr<Process>(this, [](Process*) {}));
            }
        }
    }
    else if (ins.opcode == 2 && ins.args.size() == 3) { // ADD
        const std::string& destVar = ins.args[0];
        uint16_t a = getValue(ins.args[1]);
        uint16_t b = getValue(ins.args[2]);
        if (symbolTable_.count(destVar) && memoryManager_) {
            const std::string& destAddr = symbolTable_.at(destVar);
            memoryManager_->write(destAddr, clamp(static_cast<int64_t>(a) + static_cast<int64_t>(b)), std::shared_ptr<Process>(this, [](Process*) {}));
        }
    }
    else if (ins.opcode == 3 && ins.args.size() == 3) { // SUB
        const std::string& destVar = ins.args[0];
        uint16_t a = getValue(ins.args[1]);
        uint16_t b = getValue(ins.args[2]);
        if (symbolTable_.count(destVar) && memoryManager_) {
            const std::string& destAddr = symbolTable_.at(destVar);
            memoryManager_->write(destAddr, clamp(static_cast<int64_t>(a) - static_cast<int64_t>(b)), std::shared_ptr<Process>(this, [](Process*) {}));
        }
    }
    else if (ins.opcode == 4) { // PRINT
        std::string output = "Hello world from " + name_ + "!";
        std::stringstream ss;
        if (coreId >= 0) {
            ss << "Core:" << coreId << " ";
        }
        ss << "\"" << output << "\"";
        logs_.emplace_back(time(nullptr), ss.str());
    }
    else if (ins.opcode == 5 && ins.args.size() == 1) { // SLEEP
        uint8_t ticks = static_cast<uint8_t>(getValue(ins.args[0]));
        isSleeping_ = true;
        sleepTargetTick_ = globalCpuTicks.load() + ticks;
        insCount_++;
    }
    else if (ins.opcode == 6 && ins.args.size() == 1) { // FOR(repeats)
        uint16_t repeatCount = getValue(ins.args[0]);
        if (repeatCount > 1000) repeatCount = 1000;
        if (loopStack.size() >= 3) return;
        loopStack.push_back({ insCount_ + 1, repeatCount });
    }
    else if (ins.opcode == 7) { // END
        if (!loopStack.empty()) {
            LoopState& currentLoop = loopStack.back();
            currentLoop.repeats--;
            if (currentLoop.repeats > 0) {
                insCount_ = currentLoop.startIns - 1;
            }
            else {
                loopStack.pop_back();
            }
        }
        else {
            logs_.emplace_back(time(nullptr), "[Error] END without matching FOR!");
        }
    }
    else if (ins.opcode == 8 && ins.args.size() == 2) { // READ var, memory_address
        const std::string& varName = ins.args[0];
        const std::string& sourceAddress = ins.args[1];
        if (memoryManager_ && symbolTable_.count(varName)) {
            uint16_t value = memoryManager_->read(sourceAddress, std::shared_ptr<Process>(this, [](Process*) {}));
            const std::string& destAddress = symbolTable_.at(varName);
            memoryManager_->write(destAddress, value, std::shared_ptr<Process>(this, [](Process*) {}));
        }
    }
    else if (ins.opcode == 9 && ins.args.size() == 2) { // WRITE memory_address, value
        const std::string& destAddress = ins.args[0];
        uint16_t value = getValue(ins.args[1]);
        if (memoryManager_) {
            memoryManager_->write(destAddress, value, std::shared_ptr<Process>(this, [](Process*) {}));
        }
    }
}

void Process::genRandInst(uint64_t min_ins, uint64_t max_ins) {
    insList.clear();
    logs_.clear();
    // CHANGED: Dana - Clear the symbol table and reset the offset instead of the old vars map.
    symbolTable_.clear();
    symbolTableOffset_ = 0;
    loopStack.clear();
    insCount_ = 0;

    // ... (rest of genRandInst is mostly unchanged but would benefit from using the new system)
    // For now, it will still generate valid instructions.
    std::uniform_int_distribution<uint64_t> distInstructions(min_ins, max_ins);
    uint64_t totalInstructions = distInstructions(gen);

    std::vector<std::string> varPool = { "x", "y", "z", "a", "b", "c" };
    std::uniform_int_distribution<int> distVar(0, static_cast<int>(varPool.size()) - 1);
    std::uniform_int_distribution<int> distValue(0, 1000);
    std::uniform_int_distribution<int> distSmallValue(0, 100);
    std::uniform_int_distribution<int> distSleepTicks(1, 10);
    std::uniform_real_distribution<double> distProbability(0.0, 1.0);
    std::vector<int> opcode_pool = { 1, 2, 3, 4, 5, 8, 9 };
    std::uniform_int_distribution<int> distGeneralOp(0, static_cast<int>(opcode_pool.size()) - 1);

    int currentDepth = 0;
    uint64_t instructionsGenerated = 0;

    while (instructionsGenerated < totalInstructions) {
        int opcode;
        bool allowFor = (currentDepth < 3);

        if (allowFor && distProbability(gen) < 0.15 &&
            instructionsGenerated + 3 <= totalInstructions) {
            opcode = 6;
        }
        else {
            opcode = opcode_pool[distGeneralOp(gen)];
        }

        Instruction ins;
        ins.opcode = opcode;

        switch (opcode) {
        case 1:
            ins.args.push_back(varPool[distVar(gen)]);
            if (distProbability(gen) < 0.5) {
                ins.args.push_back(std::to_string(distValue(gen)));
            }
            break;
        case 2:
        case 3:
            ins.args.push_back(varPool[distVar(gen)]);
            ins.args.push_back(varPool[distVar(gen)]);
            ins.args.push_back(std::to_string(distSmallValue(gen)));
            break;
        case 4:
            break;
        case 5:
            ins.args.push_back(std::to_string(distSleepTicks(gen)));
            break;
        case 8: // READ
            ins.args.push_back(varPool[distVar(gen)]);
            {
                std::stringstream ss;
                ss << "0x" << std::hex << distValue(gen);
                ins.args.push_back(ss.str());
            }
            break;
        case 9: // WRITE
        {
            std::stringstream ss;
            ss << "0x" << std::hex << distValue(gen);
            ins.args.push_back(ss.str());
        }
        ins.args.push_back(std::to_string(distValue(gen)));
        break;
        case 6: {
            std::uniform_int_distribution<int> distRepeats(1, 5);
            ins.args.push_back(std::to_string(distRepeats(gen)));
            insList.push_back(ins);
            instructionsGenerated++;
            currentDepth++;
            int maxBlock = static_cast<int>(totalInstructions - instructionsGenerated - 1);
            std::uniform_int_distribution<int> distBlock(1, std::min(5, maxBlock));
            int blockSize = distBlock(gen);
            for (int i = 0; i < blockSize; ++i) {
                Instruction body;
                int innerOpcode = opcode_pool[distGeneralOp(gen)];
                if (innerOpcode == 6 && currentDepth >= 3) continue;
                body.opcode = innerOpcode;
                // ... (inner switch cases for instruction bodies)
                insList.push_back(body);
                instructionsGenerated++;
            }
            Instruction endIns;
            endIns.opcode = 7; // END
            insList.push_back(endIns);
            instructionsGenerated++;
            currentDepth--;
            continue;
        }
        }
        insList.push_back(ins);
        instructionsGenerated++;
    }
    while (currentDepth > 0 && instructionsGenerated < totalInstructions) {
        Instruction endIns;
        endIns.opcode = 7;
        insList.push_back(endIns);
        instructionsGenerated++;
        currentDepth--;
    }
    if (insList.size() > totalInstructions) {
        insList.resize(totalInstructions);
    }
}

bool Process::runOneInstruction(int coreId) {
    if (finished_) return false;

    if (isSleeping_) {
        if (globalCpuTicks.load() >= sleepTargetTick_) {
            isSleeping_ = false;
            sleepTargetTick_ = 0;
        }
        else {
            return false;
        }
    }

    if (insCount_ >= insList.size()) {
        finished_ = true;
        return false;
    }

    execute(insList[insCount_], coreId);

    if (!isSleeping_) {
        insCount_++;
    }

    if (insCount_ >= insList.size()) {
        finished_ = true;
        return false;
    }

    return true;
}

std::string Process::smi() const {
    std::stringstream ss;
    ss << "Process name: " << name_ << "\n";
    ss << "ID: " << pid_ << "\n";

    ss << "Logs:\n";
    if (logs_.empty()) {
        ss << "  (No logs yet)\n";
    }
    else {
        for (const auto& kv : logs_) {
            time_t timestamp = kv.first;
            const std::string& message = kv.second;
            tm localtm{};
#ifdef _WIN32
            localtime_s(&localtm, &timestamp);
#else
            localtime_r(&timestamp, &localtm);
#endif
            char buf[64];
            strftime(buf, sizeof(buf), "(%m/%d/%Y %I:%M:%S%p)", &localtm);
            ss << "  " << buf << " " << message << "\n";
        }
    }

    if (finished_) {
        ss << "Finished!\n";
    }
    else if (isSleeping_) {
        ss << "Status: Sleeping (Until tick: " << sleepTargetTick_ << ")\n";
    }
    else {
        ss << "Status: Running\n";
    }

    ss << "Current instruction line: " << insCount_ << "\n";
    ss << "Lines of code: " << insList.size() << "\n";

    // CHANGED: Dana - The "Variables" section now reads from the symbol table and fetches values from memory.
    ss << "Variables:\n";
    if (symbolTable_.empty()) {
        ss << "  (No variables declared)\n";
    }
    else {
        for (const auto& pair : symbolTable_) {
            const std::string& varName = pair.first;
            const std::string& address = pair.second;
            uint16_t value = 0;
            if (memoryManager_) {
                // This const_cast is not ideal, but necessary because smi() is const.
                // It's safe here because read() doesn't modify the process state.
                Process* nonConstThis = const_cast<Process*>(this);
                value = memoryManager_->read(address, std::shared_ptr<Process>(nonConstThis, [](Process*) {}));
            }
            ss << "  " << varName << " = " << value << " @ " << address << "\n";
        }
    }

    return ss.str();
}
