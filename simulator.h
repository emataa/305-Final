#ifndef SIMULATOR_H
#define SIMULATOR_H

#include "instruction.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <fstream>

class PipelineSimulator
{
public:
    // setup the sim with a specific depth config (1-4)
    PipelineSimulator(int depth_config);

    // read the trace and run the cycle-by-cycle sim
    void run(const std::string &trace_file, long long start_inst, long long inst_count);

    // show the final cycle count and instruction breakdown
    void printStats() const;

private:
    // small wrapper so each stage can hold a valid/invalid instruction slot
    struct PipeSlot
    {
        bool valid;
        long long seq; // global program order inside this replication
        Instruction inst;

        PipeSlot()
            : valid(false), seq(-1), inst(0, 1, std::vector<uint64_t>()) {}
    };

    int D;            // pipeline depth setting (1, 2, 3, or 4)
    double frequency; // clock speed in ghz for the current d setting
    long long clock;  // current simulation cycle

    long long retired_count;  // keeps track of how many instructions finished
    long long fetched_count;  // how many instructions have been fetched for this run
    long long next_seq;       // sequence number for in-order tracking
    long long type_counts[6]; // counts for each instruction type (1-5)

    std::string current_trace;
    int width;
    long long current_start;
    long long current_count;

    // fixed 2-wide pipeline stages
    PipeSlot if_stage[2];
    PipeSlot id_stage[2];
    PipeSlot ex_stage[2];
    PipeSlot mem_stage[2];
    PipeSlot wb_stage[2];

    // tracks when data becomes ready for dependencies (pc -> cycle)
    std::unordered_map<uint64_t, long long> last_done_cycle;

    // control hazard flag to block fetch until the cycle after a branch finishes EX
    bool branch_stall;

    // structural hazard flags for single-unit resources (reset every cycle)
    bool int_busy, fp_busy, br_busy, load_busy, store_busy;

    // sparse trace index: every Nth line we remember a file offset
    std::vector<std::streamoff> trace_checkpoints;
    long long total_trace_lines;

    static const long long INDEX_STRIDE = 100000;

    // resets pipeline state for a new simulation run
    void resetState();

    // stage helpers
    void compactStage(PipeSlot stage[2]);
    bool stageEmpty(const PipeSlot stage[2]) const;
    bool pipelineEmpty() const;

    // latency helpers
    int getEXLatency(const Instruction &inst) const;
    int getMEMLatency(const Instruction &inst) const;

    // dependency and parsing helpers
    bool depsReady(const Instruction &inst) const;
    bool parseTraceLine(const std::string &line, Instruction &out_inst) const;
    bool readNextInstruction(std::ifstream &file, Instruction &out_inst);

    // sparse index helpers
    bool loadTraceIndex(const std::string &trace_file);
    bool buildTraceIndex(const std::string &trace_file);
    bool ensureTraceIndex(const std::string &trace_file);
    bool seekToInstruction(std::ifstream &file, long long start_inst);
};

#endif