#ifndef SIMULATOR_H
#define SIMULATOR_H

#include "instruction.h"
#include <string>
#include <vector>
#include <queue>
#include <unordered_map>
#include <cstdint>
#include <deque>

class PipelineSimulator {
public:
    PipelineSimulator(int depth_config);
    void run(const std::string& trace_file, long long start_inst, long long inst_count);
    void printStats() const;

private:
    int D;
    double frequency;
    long long clock;
    long long retired_count;
    long long type_counts[6]; // 1-5 indexing

    std::deque<Instruction> window; // Instructions currently in pipeline 
    std::unordered_map<uint64_t, long long> last_done_cycle; // PC -> Cycle completed
    
    bool branch_stall; // Blocks fetch until branch finishes EX 

    // Functional Unit Status (reset every cycle) 
    bool int_busy, fp_busy, br_busy, load_busy, store_busy;

    void processPipeline();
    bool canMoveToEX(const Instruction& inst);
};

#endif