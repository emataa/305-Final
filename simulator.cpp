#include "simulator.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>

// constructor: sets up the pipeline depth and maps it to frequency
PipelineSimulator::PipelineSimulator(int depth_config) 
    : D(depth_config), clock(0), retired_count(0), branch_stall(false) {
    
    width = 2; // fixed 2-wide superscalar for this sim
    // frequency mapping for each depth d level
    if (D == 1) frequency = 1.0;
    else if (D == 2) frequency = 1.2;
    else if (D == 3) frequency = 1.7;
    else if (D == 4) frequency = 1.8;
    else frequency = 1.0;

    // init type counters to zero
    for(int i = 0; i < 6; ++i) type_counts[i] = 0;
}

// main execution loop: parses the trace and runs the clock
void PipelineSimulator::run(const std::string& trace_file, long long start_inst, long long inst_count) {

    current_trace = trace_file;
    current_start = start_inst;
    current_count = inst_count;
    
    std::ifstream file(trace_file);
    if (!file.is_open()) {
        std::cerr << "error: could not open trace file " << trace_file << std::endl;
        return;
    }

    std::string line;
    long long lines_read = 0;
    
    // skip instructions until we hit the start point
    while (lines_read < start_inst - 1 && std::getline(file, line)) {
        lines_read++;
    }

    // loop until we've retired the requested number of instructions
    while (retired_count < inst_count) {
        // structural hazards reset every cycle (only 1 of each unit exists)
        int_busy = fp_busy = br_busy = load_busy = store_busy = false; 

        // step 1: move existing instructions through their stages
        processPipeline();

        // step 2: fetch up to 2 new instructions (2-wide superscalar)
        // only fetch if we aren't stalled by a branch and haven't hit the count limit
        for (int i = 0; i < 2 && !branch_stall && (retired_count + window.size()) < inst_count; ++i) {
            if (std::getline(file, line)) {
                std::stringstream ss(line);
                std::string part;
                uint64_t pc; 
                int type;
                std::vector<uint64_t> deps;

                // parse csv format: pc, type, dep1, dep2...
                if (std::getline(ss, part, ',')) pc = std::stoull(part, nullptr, 16);
                if (std::getline(ss, part, ',')) type = std::stoi(part);
                while (std::getline(ss, part, ',')) {
                    if (!part.empty()) deps.push_back(std::stoull(part, nullptr, 16));
                }

                // enter the pipeline at if stage
                window.emplace_back(pc, type, deps);
            }
        }
        
        // tick the master clock
        clock++;
    }
}

// handles the movement of instructions through the 5-stage pipeline
void PipelineSimulator::processPipeline() {
    for (auto it = window.begin(); it != window.end(); ) {
        it->cycles_in_stage++;

        // wb: write back stage (finishing up)
        if (it->stage == Stage::WB) {
            type_counts[it->type]++;
            retired_count++;
            it = window.erase(it); // remove from pipeline window
            continue; 
        }

        // mem: memory access stage
        if (it->stage == Stage::MEM) {
            // loads take 3 cycles in mem if d is 3 or 4
            int mem_lat = (it->type == 4 && (D == 3 || D == 4)) ? 3 : 1;
            if (it->cycles_in_stage >= mem_lat) {
                it->stage = Stage::WB; 
                it->cycles_in_stage = 0;
                last_done_cycle[it->pc] = clock; // data ready for consumers
            }
        } 
        // ex: execution stage
        else if (it->stage == Stage::EX) {
            // floating point takes 2 cycles in ex if d is 2 or 4
            int ex_lat = (it->type == 2 && (D == 2 || D == 4)) ? 2 : 1;
            if (it->cycles_in_stage >= ex_lat) {
                if (it->type == 3) branch_stall = false; // branch resolved, resume fetch
                it->stage = Stage::MEM; 
                it->cycles_in_stage = 0;
                // non-memory ops finish their "data" here
                if (it->type <= 3) last_done_cycle[it->pc] = clock; 
            }
        } 
        // id: instruction decode / dispatch
        else if (it->stage == Stage::ID) {
            // check for data and structural hazards
            if (canMoveToEX(*it)) {
                it->stage = Stage::EX; 
                it->cycles_in_stage = 0;
                // branches stall the fetcher immediately
                if (it->type == 3) branch_stall = true; 
            }
        } 
        // if: instruction fetch
        else if (it->stage == Stage::IF) {
            it->stage = Stage::ID; 
            it->cycles_in_stage = 0;
        }
        
        ++it;
    }
}

// hazard detection logic
bool PipelineSimulator::canMoveToEX(const Instruction& inst) {
    // 1. data hazards: check if all dependencies have written back/finished
    for (auto dep : inst.dep_pcs) {
        if (last_done_cycle.find(dep) == last_done_cycle.end() || last_done_cycle[dep] >= clock) {
            return false; // producer hasn't finished yet
        }
    }

    // 2. structural hazards: check if the required functional unit is busy this cycle
    if (inst.type == 1 && int_busy) return false;
    if (inst.type == 2 && fp_busy) return false;
    if (inst.type == 3 && br_busy) return false;
    
    // claim the units if we can move
    if (inst.type == 1) int_busy = true;
    else if (inst.type == 2) fp_busy = true;
    else if (inst.type == 3) br_busy = true;

    return true;
}

// prints the final results in a clean format
void PipelineSimulator::printStats() const {
    double total = retired_count;
    double time_ns = (frequency > 0) ? (clock / frequency) : 0;

    std::cout << "\n>>> SIMULATION SUMMARY <<<" << std::endl;
    std::cout << " Source Trace  : " << current_trace << std::endl;
    std::cout << " Configuration : " << width << "-wide Superscalar (D=" << D << ")" << std::endl;
    std::cout << " Range         : Start @" << current_start << " | Length: " << current_count << std::endl;
    
    std::cout << "\n>>> PERFORMANCE DATA <<<" << std::endl;
    std::cout << std::left << std::setw(25) << " Total Cycles:" << clock << std::endl;
    std::cout << std::left << std::setw(25) << " Runtime (ns):" << std::fixed << std::setprecision(2) << time_ns << std::endl;
    std::cout << std::left << std::setw(25) << " Retired Instr:" << retired_count << std::endl;

    std::cout << "\n>>> INSTRUCTION MIX <<<" << std::endl;
    const char* types[] = {"Integer", "Floating Point", "Branch", "Load", "Store"};
    
    for (int i = 0; i < 5; i++) {
        double pct = (total > 0) ? (type_counts[i+1] / total) * 100.0 : 0.0;
        std::cout << " [" << i+1 << "] " << std::left << std::setw(18) << types[i] 
                  << " : " << pct << "%" << std::endl;
    }

    std::cout << "\nCSV_LOG: " << clock << "," << time_ns << "," 
              << (type_counts[1]/total)*100 << "," << (type_counts[2]/total)*100 << ","
              << (type_counts[3]/total)*100 << "," << (type_counts[4]/total)*100 << ","
              << (type_counts[5]/total)*100 << std::endl;
    std::cout << "-------------------------------------------\n" << std::endl;
}