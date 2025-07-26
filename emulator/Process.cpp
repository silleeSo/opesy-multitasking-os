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
    : pid_(pid), name_(std::move(name)), finished_(false), isSleeping_(false), sleepTargetTick_(0), memoryManager_(memManager),
    terminationReason_(TerminationReason::RUNNING), allocatedMemoryBytes_(0), violationTime_(0), hasBeenScheduled_(false) {
}

void Process::execute(const Instruction& ins, int coreId) {
    auto getValue = [this](const std::string& token) -> uint16_t {
        if (isdigit(token[0]) || (token[0] == '-' && token.size() > 1)) {
            try { return static_cast<uint16_t>(std::stoi(token)); }
            catch (const std::out_of_range&) { return 0; }
        }
        else {
            if (symbolTable_.count(token)) {
                const std::string& address = symbolTable_.at(token);
                if (memoryManager_) {
                    return memoryManager_->read(address, shared_from_this());
                }
            }
            return 0;
        }
        };

    auto clamp = [](int64_t val) -> uint16_t {
        if (val < 0) return 0;
        if (val > UINT16_MAX) return UINT16_MAX;
        return static_cast<uint16_t>(val);
        };

    if (ins.opcode == 1 && ins.args.size() >= 1) { // DECLARE
        if (symbolTable_.size() >= 32) {
            logs_.emplace_back(time(nullptr), "[Warning] Symbol table full. DECLARE ignored.");
            return;
        }
        const std::string& varName = ins.args[0];
        std::stringstream ss;
        ss << "0x" << std::hex << std::setw(4) << std::setfill('0') << symbolTableOffset_;
        std::string logicalAddress = ss.str();
        symbolTable_[varName] = logicalAddress;
        symbolTableOffset_ += 2;
        if (ins.args.size() == 2) {
            uint16_t initialValue = clamp(getValue(ins.args[1]));
            if (memoryManager_) memoryManager_->write(logicalAddress, initialValue, shared_from_this());
        }
    }
    else if (ins.opcode == 2 && ins.args.size() == 3) { // ADD
        const std::string& destVar = ins.args[0];
        uint16_t a = getValue(ins.args[1]);
        uint16_t b = getValue(ins.args[2]);
        if (symbolTable_.count(destVar) && memoryManager_) {
            const std::string& destAddr = symbolTable_.at(destVar);
            memoryManager_->write(destAddr, clamp(static_cast<int64_t>(a) + static_cast<int64_t>(b)), shared_from_this());
        }
    }
    else if (ins.opcode == 3 && ins.args.size() == 3) { // SUB
        const std::string& destVar = ins.args[0];
        uint16_t a = getValue(ins.args[1]);
        uint16_t b = getValue(ins.args[2]);
        if (symbolTable_.count(destVar) && memoryManager_) {
            const std::string& destAddr = symbolTable_.at(destVar);
            memoryManager_->write(destAddr, clamp(static_cast<int64_t>(a) - static_cast<int64_t>(b)), shared_from_this());
        }
    }
    else if (ins.opcode == 4) { // PRINT
        std::string output;
        if (!ins.args.empty()) {
            for (const auto& arg : ins.args) {
                if (symbolTable_.count(arg)) {
                    output += std::to_string(getValue(arg));
                }
                else {
                    output += arg;
                }
            }
        }
        logs_.emplace_back(time(nullptr), output);
    }
    else if (ins.opcode == 5 && ins.args.size() == 1) { // SLEEP
        uint8_t ticks = static_cast<uint8_t>(getValue(ins.args[0]));
        isSleeping_ = true;
        sleepTargetTick_ = globalCpuTicks.load() + ticks;
        insCount_++;
    }
    else if (ins.opcode == 6 && ins.args.size() == 1) { // FOR
        uint16_t repeatCount = getValue(ins.args[0]);
        if (repeatCount > 1000) repeatCount = 1000;
        if (loopStack.size() >= 3) return;
        loopStack.push_back({ insCount_ + 1, repeatCount });
    }
    else if (ins.opcode == 7) { // END
        if (!loopStack.empty()) {
            LoopState& currentLoop = loopStack.back();
            currentLoop.repeats--;
            if (currentLoop.repeats > 0) insCount_ = currentLoop.startIns - 1;
            else loopStack.pop_back();
        }
        else {
            logs_.emplace_back(time(nullptr), "[Error] END without matching FOR!");
        }
    }
    else if (ins.opcode == 8 && ins.args.size() == 2) { // READ
        const std::string& varName = ins.args[0];
        const std::string& sourceAddress = ins.args[1];
        if (memoryManager_ && symbolTable_.count(varName)) {
            uint16_t value = memoryManager_->read(sourceAddress, shared_from_this());
            const std::string& destAddress = symbolTable_.at(varName);
            memoryManager_->write(destAddress, value, shared_from_this());
        }
    }
    else if (ins.opcode == 9 && ins.args.size() == 2) { // WRITE
        const std::string& destAddress = ins.args[0];
        uint16_t value = getValue(ins.args[1]);
        if (memoryManager_) {
            memoryManager_->write(destAddress, value, shared_from_this());
        }
    }
}

void Process::setTerminationReason(TerminationReason reason, const std::string& addr) {
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

void Process::loadInstructionsFromString(const std::string& instruction_str) {
    insList.clear();
    std::stringstream ss(instruction_str);
    std::string segment;
    std::unordered_map<std::string, uint8_t> opcodeMap = {
        {"DECLARE", 1}, {"ADD", 2}, {"SUB", 3}, {"PRINT", 4},
        {"SLEEP", 5}, {"FOR", 6}, {"END", 7}, {"READ", 8}, {"WRITE", 9}
    };

    while (std::getline(ss, segment, ';')) {
        segment.erase(0, segment.find_first_not_of(" \t\n\r"));
        segment.erase(segment.find_last_not_of(" \t\n\r") + 1);
        if (segment.empty()) continue;

        std::stringstream ins_ss(segment);
        std::string opcode_str;
        ins_ss >> opcode_str;

        if (opcodeMap.count(opcode_str)) {
            Instruction inst;
            inst.opcode = opcodeMap[opcode_str];
            std::string arg;

            if (inst.opcode == 4) {
                if (std::getline(ins_ss, arg)) {
                    arg.erase(0, arg.find_first_not_of(" \t"));
                    if (arg.front() == '(' && arg.back() == ')') {
                        arg = arg.substr(1, arg.length() - 2);
                    }
                    inst.args.push_back(arg);
                }
            }
            else {
                while (ins_ss >> arg) {
                    inst.args.push_back(arg);
                }
            }
            insList.push_back(inst);
        }
    }
}

void Process::genRandInst(uint64_t min_ins, uint64_t max_ins) {
    insList.clear();
    logs_.clear();
    symbolTable_.clear();
    symbolTableOffset_ = 0;
    loopStack.clear();
    insCount_ = 0;

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
    if (isFinished()) return false;

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
        setTerminationReason(TerminationReason::FINISHED_NORMALLY);
        return false;
    }

    execute(insList[insCount_], coreId);

    if (isFinished()) return false;

    if (!isSleeping_) {
        insCount_++;
    }

    if (insCount_ >= insList.size()) {
        setTerminationReason(TerminationReason::FINISHED_NORMALLY);
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

    if (terminationReason_ == TerminationReason::MEMORY_VIOLATION) {
        ss << "Status: Terminated (Memory Access Violation)\n";
    }
    else if (isFinished()) {
        ss << "Status: Finished!\n";
    }
    else if (isSleeping_) {
        ss << "Status: Sleeping (Until tick: " << sleepTargetTick_ << ")\n";
    }
    else {
        ss << "Status: Running\n";
    }

    ss << "Current instruction line: " << insCount_ << "\n";
    ss << "Lines of code: " << insList.size() << "\n";

    ss << "Variables:\n";
    if (symbolTable_.empty()) {
        ss << "  (No variables declared)\n";
    }
    else {
        for (const auto& pair : symbolTable_) {
            const std::string& varName = pair.first;
            const std::string& address = pair.second;
            uint16_t value = 0;
            if (memoryManager_ && terminationReason_ != TerminationReason::MEMORY_VIOLATION) {
                try {
                    Process* nonConstThis = const_cast<Process*>(this);
                    value = memoryManager_->read(address, nonConstThis->shared_from_this());
                }
                catch (const std::runtime_error&) {
                }
            }
            ss << "  " << varName << " = " << value << " @ " << address << "\n";
        }
    }

    return ss.str();
}

int Process::getSymbolTablePages(int frameSize) const {
    if (frameSize <= 0) return 0;
    // CHANGED: Dana - Added a static_cast to int to resolve the C4267 conversion warning.
    return static_cast<int>((symbolTableOffset_ + frameSize - 1) / frameSize);
}