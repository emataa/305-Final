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
    // setup the sim with a specific depth config (1-4) 
    PipelineSimulator(int depth_config);
    // read the trace and run the cycle-by-cycle sim 
    void run(const std::string& trace_file, long long start_inst, long long inst_count);

    // show the final cycle count and instruction breakdown 
    void printStats() const;

private:
    int D;             // pipeline depth setting (1, 2, 3, or 4)
    double frequency;  // clock speed in ghz for the current d setting 
    long long clock;   // current simulation cycle 
    
    long long retired_count;  // keeps track of how many instructions finished 
    long long type_counts[6]; // counts for each instruction type (1-5) 
    std::string current_trace;
    int width; 
    long long current_start;
    long long current_count;
    // our "instruction window" for all instructions in progress 
    std::deque<Instruction> window; 

    // tracks when data becomes ready for dependencies (pc -> cycle)
    std::unordered_map<uint64_t, long long> last_done_cycle; 
    
    // control hazard flag to block fetch until a branch clears ex 
    bool branch_stall; 

    // structural hazard flags for single-unit resources (reset every cycle) 
    bool int_busy, fp_busy, br_busy, load_busy, store_busy;

    // updates every instruction's stage each cycle 
    void processPipeline();

    // checks data deps and hardware availability before moving to ex 
    bool canMoveToEX(const Instruction& inst);
};

#endif