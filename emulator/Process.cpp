
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

static std::random_device rd;
static std::mt19937 gen(rd());

Process::Process(uint64_t pid, std::string name)
    : pid_(pid), name_(std::move(name)), finished_(false), isSleeping_(false), sleepTargetTick_(0) {}

void Process::execute(const Instruction& ins, int coreId) {
    auto getValue = [this](const std::string& token) -> uint16_t {
        if (isdigit(token[0]) || (token[0] == '-' && token.size() > 1)) {
            try {
                return static_cast<uint16_t>(std::stoi(token));
            }
            catch (const std::out_of_range&) {
                return 0;
            }
        }
        else {
            return vars.count(token) ? vars[token] : 0;
        }
        };

    auto clamp = [](int64_t val) -> uint16_t {
        if (val < 0) return 0;
        if (val > UINT16_MAX) return UINT16_MAX;
        return static_cast<uint16_t>(val);
        };

    if (ins.opcode == 1 && ins.args.size() >= 1) {
        const std::string& var = ins.args[0];
        uint16_t value = ins.args.size() == 2 ? clamp(getValue(ins.args[1])) : 0;
        vars[var] = value;
    }
    else if (ins.opcode == 2 && ins.args.size() == 3) {
        const std::string& dest = ins.args[0];
        uint16_t a = getValue(ins.args[1]);
        uint16_t b = getValue(ins.args[2]);
        vars[dest] = clamp(static_cast<int64_t>(a) + static_cast<int64_t>(b));
    }
    else if (ins.opcode == 3 && ins.args.size() == 3) {
        const std::string& dest = ins.args[0];
        uint16_t a = getValue(ins.args[1]);
        uint16_t b = getValue(ins.args[2]);
        vars[dest] = clamp(static_cast<int64_t>(a) - static_cast<int64_t>(b));
    }
    else if (ins.opcode == 4) {
        std::string output = "Hello world from " + name_ + "!";

        std::stringstream ss;
        if (coreId >= 0) {
            ss << "Core:" << coreId << " ";
        }
        ss << "\"" << output << "\"";

        logs_.emplace_back(time(nullptr), ss.str());
    }

    else if (ins.opcode == 5 && ins.args.size() == 1) {
        uint8_t ticks = static_cast<uint8_t>(getValue(ins.args[0]));
        isSleeping_ = true;
        sleepTargetTick_ = globalCpuTicks.load() + ticks;
        insCount_++;
    }
    else if (ins.opcode == 6 && ins.args.size() == 1) { // FOR(repeats)
        uint16_t repeatCount = getValue(ins.args[0]);

        // Clamp to prevent extremely large repeats
        if (repeatCount > 1000) repeatCount = 1000;

        // Enforce maximum nesting level
        if (loopStack.size() >= 3) {
            // logs_.emplace_back(time(nullptr), "[Error] Maximum FOR nesting exceeded during execution. Skipping this FOR instruction.");
            return;
        }

        // Push loop state to stack
        LoopState loop = { insCount_ + 1, repeatCount };
        loopStack.push_back(loop);
    }

    else if (ins.opcode == 7) { // END
        if (!loopStack.empty()) {
            LoopState& currentLoop = loopStack.back();
            currentLoop.repeats--;

            if (currentLoop.repeats > 0) {
                insCount_ = currentLoop.startIns - 1; // Jump back
            }
            else {
                loopStack.pop_back();
            }
        }
        else {
            logs_.emplace_back(time(nullptr), "[Error] END without matching FOR! This indicates a program generation error.");
        }
    }
}

void Process::genRandInst(uint64_t min_ins, uint64_t max_ins) {
    insList.clear();
    logs_.clear();
    vars.clear();
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
    std::vector<int> opcode_pool = { 1, 2, 3, 4, 5 };
    std::uniform_int_distribution<int> distGeneralOp(0, static_cast<int>(opcode_pool.size()) - 1);

    int currentDepth = 0;
    uint64_t instructionsGenerated = 0;

    while (instructionsGenerated < totalInstructions) {
        int opcode;
        bool allowFor = (currentDepth < 3);

        // Allow FOR only if there's room for FOR + END + at least 1 body instruction
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

        case 6: {
            // FOR loop
            std::uniform_int_distribution<int> distRepeats(1, 5);
            ins.args.push_back(std::to_string(distRepeats(gen)));
            insList.push_back(ins); // FOR
            instructionsGenerated++;
            currentDepth++;

            // Determine max allowed block size to stay within totalInstructions
            int maxBlock = static_cast<int>(totalInstructions - instructionsGenerated - 1); // 1 for END
            std::uniform_int_distribution<int> distBlock(1, std::min(5, maxBlock));
            int blockSize = distBlock(gen);

            for (int i = 0; i < blockSize; ++i) {
                Instruction body;
                int innerOpcode;

                if (currentDepth < 3 && distProbability(gen) < 0.15 &&
                    instructionsGenerated + 3 <= totalInstructions) {
                    innerOpcode = 6;
                }
                else {
                    innerOpcode = opcode_pool[distGeneralOp(gen)];
                }

                if (innerOpcode == 6 && currentDepth >= 3) continue;

                body.opcode = innerOpcode;

                switch (innerOpcode) {
                case 1:
                    body.args.push_back(varPool[distVar(gen)]);
                    body.args.push_back(std::to_string(distValue(gen)));
                    break;
                case 2:
                case 3:
                    body.args.push_back(varPool[distVar(gen)]);
                    body.args.push_back(varPool[distVar(gen)]);
                    body.args.push_back(std::to_string(distSmallValue(gen)));
                    break;
                case 4:
                    break;
                case 5:
                    body.args.push_back(std::to_string(distSleepTicks(gen)));
                    break;
                }

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

    // Final cleanup: add END instructions if FORs were left open
    while (currentDepth > 0 && instructionsGenerated < totalInstructions) {
        Instruction endIns;
        endIns.opcode = 7;
        insList.push_back(endIns);
        instructionsGenerated++;
        currentDepth--;
    }

    // Clamp to exact instruction count, just in case
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
            return false;  // still sleeping
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
        // logs_ should be std::vector<std::pair<time_t, std::string>>
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

            // Use buf directly to avoid << ambiguity
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

    /*ss << "Variables:\n";
    if (vars.empty()) {
        ss << "  (No variables declared)\n";
    }
    else {
        for (const auto& kv : vars) {
            ss << "  " << kv.first << " = " << kv.second << "\n";
        }
    }*/

    return ss.str();
}