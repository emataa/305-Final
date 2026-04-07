#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include <vector>
#include <cstdint>

// the 5 standard stages
enum class Stage
{
    IF,
    ID,
    EX,
    MEM,
    WB,
    RETIRED
};

struct Instruction
{
    // data from the trace File
    uint64_t pc;                   // hex address of the instruction
    int type;                      // 1:INT, 2:FP, 3:BR, 4:LD, 5:ST
    std::vector<uint64_t> dep_pcs; // instructions we're waiting on (RAW hazards)

    // simulation State
    Stage stage;         // where it is in the pipeline right now
    int cycles_in_stage; // counter for multi-cycle EX/MEM (D configs)
    long long ready_at;  // cycle when our result is ready for dependents
    bool is_stalled;     // helpful for debugging ID stage blocks

    // constructor
    // everything starts at IF with 0 cycles clocked in
    Instruction(uint64_t addr, int t, std::vector<uint64_t> deps)
        : pc(addr), type(t), dep_pcs(std::move(deps)),
          stage(Stage::IF), cycles_in_stage(0), ready_at(-1), is_stalled(false) {}
};

#endif