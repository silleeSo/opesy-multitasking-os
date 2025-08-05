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

// Constructor for the Process class
Process::Process(uint64_t pid, std::string name, MemoryManager* memManager)
    : pid_(pid), name_(std::move(name)), finished_(false), isSleeping_(false), sleepTargetTick_(0), memoryManager_(memManager),
    terminationReason_(TerminationReason::RUNNING), allocatedMemoryBytes_(0), violationTime_(0), hasBeenScheduled_(false) {
}

// Executes a single instruction depending on opcode type
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
        const std::string& varName = ins.args[0];

        // CHANGE: Check against allocatedMemoryBytes_ instead of fixed 64.
        // Each variable is 2 bytes, so max variables is allocatedMemoryBytes_ / 2.
        if (symbolTable_.size() * 2 < allocatedMemoryBytes_) {
            std::string logicalAddress = memoryManager_->allocateVariable(shared_from_this(), varName);

            if (!logicalAddress.empty()) {
                if (ins.args.size() == 2) {
                    uint16_t initialValue = clamp(getValue(ins.args[1]));
                    if (memoryManager_) memoryManager_->write(logicalAddress, initialValue, shared_from_this());
                }
            }
            else {
                // This else block is now more likely to be hit and is correct.
                std::lock_guard<std::mutex> lock(logsMutex_);
                logs_.emplace_back(time(nullptr), "[Warning] Cannot declare '" + varName + "'. Memory allocation failed.");
            }
        }
        else {
            // CHANGE: This block will be executed when the process's memory is full.
            std::lock_guard<std::mutex> lock(logsMutex_);
            logs_.emplace_back(time(nullptr), "[Warning] Process memory full. DECLARE for '" + varName + "' ignored.");
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
        std::string output_message;

        auto stripAndTrim = [](std::string s) {
            size_t first = s.find_first_not_of(" \t\n\r");
            if (std::string::npos == first) s.clear();
            else s = s.substr(first, (s.find_last_not_of(" \t\n\r") - first + 1));

            bool hasLeadingBackslashQuote = (s.length() >= 2 && s.front() == '\\' && s[1] == '"');
            bool hasTrailingBackslashQuote = (s.length() >= 2 && s.back() == '"' && s[s.length() - 2] == '\\');


            if (!s.empty() && s.length() >= 2 &&
                ((s.front() == '"' && s.back() == '"') || (hasLeadingBackslashQuote && hasTrailingBackslashQuote))) {
                std::string stripped = s.substr(1, s.length() - 2);

                if (hasLeadingBackslashQuote) {
                    stripped = stripped.substr(1); 
                }
                if (hasTrailingBackslashQuote) {
                    
                    if (!stripped.empty() && stripped.back() == '\\') { 
                        stripped.pop_back();
                    }
                }
                return stripped;
            }
            return s;
            };

        if (!ins.args.empty()) {
            std::string full_print_argument = ins.args[0];
            std::stringstream arg_splitter(full_print_argument);
            std::string part;

            while (std::getline(arg_splitter, part, '+')) {
                std::string processed_part = stripAndTrim(part); 

                if (!processed_part.empty()) {
                    if (symbolTable_.count(processed_part)) {
                        uint16_t varValue = getValue(processed_part);
                        output_message += std::to_string(varValue);
                    }
                    else {
                        output_message += processed_part;
                    }
                }
            }
        }
        std::lock_guard<std::mutex> lock(logsMutex_);
        logs_.emplace_back(time(nullptr), output_message);
    }
    else if (ins.opcode == 5 && ins.args.size() == 1) { // SLEEP
        uint8_t ticks = static_cast<uint8_t>(getValue(ins.args[0]));
        isSleeping_ = true;
        sleepTargetTick_ = globalCpuTicks.load() + ticks;
    }
    else if (ins.opcode == 6 && ins.args.size() == 1) { 
        uint16_t repeatCount = getValue(ins.args[0]);
        if (repeatCount > 1000) repeatCount = 1000;
        if (loopStack.size() >= 3) return;

        loopStack.push_back({ insCount_, repeatCount });
    }
    else if (ins.opcode == 7) { // END
        if (!loopStack.empty()) {
            LoopState& currentLoop = loopStack.back();
            currentLoop.repeats--;
            if (currentLoop.repeats > 0) {
                insCount_ = currentLoop.startIns;
            }
            else {
                loopStack.pop_back();
            }
        }
        else {
            std::lock_guard<std::mutex> lock(logsMutex_);
            logs_.emplace_back(time(nullptr), "[Error] END without matching FOR!");
        }
    }
    else if (ins.opcode == 8 && ins.args.size() == 2) { // READ
        const std::string& varName = ins.args[0];
        const std::string& sourceAddress = ins.args[1];

        if (!symbolTable_.count(varName)) {
            // CHANGE: Check against allocatedMemoryBytes_ instead of fixed 64.
            if (symbolTable_.size() * 2 < allocatedMemoryBytes_) {
                std::string logicalAddress = memoryManager_->allocateVariable(shared_from_this(), varName);
                if (logicalAddress.empty()) {
                    std::lock_guard<std::mutex> lock(logsMutex_);
                    logs_.emplace_back(time(nullptr), "[Warning] Cannot declare '" + varName + "'. Memory allocation failed.");
                    return;
                }
            }
            else {
                std::lock_guard<std::mutex> lock(logsMutex_);
                logs_.emplace_back(time(nullptr), "[Warning] Process memory full. READ for '" + varName + "' ignored.");
                return;
            }
        }

        if (memoryManager_) {
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

// Loads instructions from a single string and parses them into the instruction list
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

        size_t opcode_end = segment.find_first_of(" ("); 
        std::string first_word;
        std::string remainder_of_segment; 

        if (opcode_end == std::string::npos) { 
            first_word = segment;
            remainder_of_segment = "";
        }
        else {
            first_word = segment.substr(0, opcode_end);
            remainder_of_segment = segment.substr(opcode_end); 
        }

        first_word.erase(0, first_word.find_first_not_of(" \t\n\r")); 
        first_word.erase(first_word.find_last_not_of(" \t\n\r") + 1); 

        if (opcodeMap.count(first_word)) {
            Instruction inst;
            inst.opcode = opcodeMap[first_word];

            std::string args_portion = remainder_of_segment; 
            args_portion.erase(0, args_portion.find_first_not_of(" \t")); 

            if (inst.opcode == 4) { // PRINT
                size_t open_paren = args_portion.find('(');
                size_t close_paren = args_portion.rfind(')');

                if (open_paren != std::string::npos && close_paren != std::string::npos && close_paren > open_paren) {
                    std::string arg_content = args_portion.substr(open_paren + 1, close_paren - open_paren - 1);
                    inst.args.push_back(arg_content);
                }
                else {
                    inst.args.push_back(args_portion);
                }
            }
            else { 
                std::stringstream args_parser(args_portion);
                std::string arg;
                while (args_parser >> arg) {
                    inst.args.push_back(arg);
                }
            }
            insList.push_back(inst);
        }
        else {
        }
    }
}

void Process::genRandInst(uint64_t min_ins, uint64_t max_ins, int memorySize) {
    insList.clear();
    logs_.clear();
    symbolTable_.clear();
    symbolTableOffset_ = 0;
    loopStack.clear();
    insCount_ = 0;

    std::uniform_int_distribution<uint64_t> distInstructions(min_ins, max_ins);
    uint64_t totalInstructions = distInstructions(gen);

    // CHANGE: Adjust the variable pool based on memory size.
    // An 8-byte process can only hold 4 variables (8/2).
    std::vector<std::string> varPool = { "x", "y", "z", "a", "b", "c" };
    if (memorySize <= 8) {
        varPool.resize(memorySize / 2); // Restrict to a maximum of 4 variables for an 8-byte process.
    }
    std::uniform_int_distribution<int> distVar(0, static_cast<int>(varPool.size()) - 1);
    std::uniform_int_distribution<int> distValue(0, 1000);
    std::uniform_int_distribution<int> distSmallValue(0, 100);
    std::uniform_int_distribution<int> distSleepTicks(1, 10);
    std::uniform_real_distribution<double> distProbability(0.0, 1.0);

    // Define memory layout constants to respect the symbol table segment
    const int SYMBOL_TABLE_SIZE = 64;
    const int GENERAL_MEM_BASE = SYMBOL_TABLE_SIZE;
    const int generalMemorySize = memorySize - SYMBOL_TABLE_SIZE;

    // Create separate opcode pools based on whether general memory is available
    std::vector<int> opcode_pool_full = { 1, 2, 3, 4, 5, 8, 9 };
    std::vector<int> opcode_pool_no_mem = { 1, 2, 3, 4, 5 };

    bool canUseGeneralMemory = (generalMemorySize > 0);
    const auto& current_opcode_pool = canUseGeneralMemory ? opcode_pool_full : opcode_pool_no_mem;
    std::uniform_int_distribution<int> distGeneralOp(0,
        static_cast<int>(current_opcode_pool.size()) - 1);

    // Create a separate distribution specifically for general memory addresses
    int num_general_words = canUseGeneralMemory ? (generalMemorySize / 2) : 0;
    std::uniform_int_distribution<int> distGeneralWord(0, num_general_words > 0 ? num_general_words - 1 : 0);

    int currentDepth = 0;
    uint64_t instructionsGenerated = 0;

    while (instructionsGenerated < totalInstructions) {
        int opcode = current_opcode_pool[distGeneralOp(gen)];

        if (opcode == 6 && (currentDepth >= 3 || instructionsGenerated + 3 > totalInstructions)) {
            continue;
        }

        Instruction ins;
        ins.opcode = opcode;

        switch (opcode) {
        case 1: // DECLARE
            // Ensure we don't try to declare more variables than memory can hold
            if (varPool.empty() || symbolTable_.size() * 2 >= memorySize) {
                continue;
            }
            ins.args.push_back(varPool[distVar(gen)]);
            if (distProbability(gen) < 0.5) {
                ins.args.push_back(std::to_string(distValue(gen)));
            }
            break;
        case 2: // ADD
        case 3: // SUB
            ins.args.push_back(varPool[distVar(gen)]);
            ins.args.push_back(varPool[distVar(gen)]);
            ins.args.push_back(std::to_string(distSmallValue(gen)));
            break;
        case 4: // PRINT
            ins.args.push_back(varPool[distVar(gen)]);
            break;
        case 5: // SLEEP
            ins.args.push_back(std::to_string(distSleepTicks(gen)));
            break;
        case 8: // READ
        case 9: // WRITE
            if (!canUseGeneralMemory) continue;

            if (opcode == 8) ins.args.push_back(varPool[distVar(gen)]);

            {
                std::stringstream ss;
                // CORRECTED LOGIC: The byte address must be within the general memory space,
                // which starts at GENERAL_MEM_BASE and ends at memorySize.
                int random_byte_address = GENERAL_MEM_BASE + distGeneralWord(gen) * 2;
                ss << "0x" << std::hex << random_byte_address;
                ins.args.push_back(ss.str());
            }

            if (opcode == 9) ins.args.push_back(std::to_string(distValue(gen)));
            break;

        case 6: { // FOR
            std::uniform_int_distribution<int> distRepeats(1, 5);
            ins.args.push_back(std::to_string(distRepeats(gen)));
            insList.push_back(ins);
            instructionsGenerated++;
            currentDepth++;

            int maxBlock = static_cast<int>(totalInstructions - instructionsGenerated - 1);
            if (maxBlock <= 0) {
                currentDepth--;
                insList.pop_back();
                instructionsGenerated--;
                continue;
            }

            std::uniform_int_distribution<int> distBlock(1, std::min(5, maxBlock));
            int blockSize = distBlock(gen);

            for (int i = 0; i < blockSize; ++i) {
                Instruction body;
                std::uniform_int_distribution<int> inner_dist(0, static_cast<int>(current_opcode_pool.size()) - 1);
                int innerOpcode = current_opcode_pool[inner_dist(gen)];
                if (innerOpcode == 6) continue;
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

    while (currentDepth > 0) {
        Instruction endIns;
        endIns.opcode = 7;
        insList.push_back(endIns);
        currentDepth--;
    }

    if (insList.size() > totalInstructions) {
        insList.resize(totalInstructions);
    }
}

// Executes one instruction step for the process, returns false if finished or sleeping
bool Process::runOneInstruction(int coreId) {
    if (isFinished()) {
        return false;
    }

    if (isSleeping_) {
        if (globalCpuTicks.load() >= sleepTargetTick_) {
            isSleeping_ = false;
        }
        else {
            return false; // Still sleeping
        }
    }

    if (insCount_ >= insList.size()) {
        setTerminationReason(TerminationReason::FINISHED_NORMALLY);
        return false;
    }

    const Instruction& currentIns = insList[insCount_];

    uint64_t instructionIndexBeforeExecution = insCount_;

    execute(currentIns, coreId);

    if (insCount_ == instructionIndexBeforeExecution) {
        insCount_++;
    }

    if (isFinished()) {
        return false;
    }

    return true;
}

std::string Process::smi() const {
    std::stringstream ss;
    ss << "Process name: " << name_ << "\n";
    ss << "ID: " << pid_ << "\n";

    ss << "Logs:\n";
    {
        std::lock_guard<std::mutex> lock(logsMutex_); // Add this lock
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
                catch (const std::exception& e) {
                    std::cerr << "Error getting variable value in smi(): " << e.what() << std::endl;
                }
            }
            ss << "  " << varName << " = " << value << " @ " << address << "\n";
        }
    }

    return ss.str();
}

// Calculates the number of pages used by the symbol table based on frame size
int Process::getSymbolTablePages(int frameSize) const {
    if (frameSize <= 0) return 0;
    return static_cast<int>((symbolTableOffset_ + frameSize - 1) / frameSize);
}

// For debugging
Instruction Process::getCurrentInstruction() const {
    if (insCount_ < insList.size()) {
        return insList[insCount_];
    }
    return {}; // Return empty instruction if out of bounds
}