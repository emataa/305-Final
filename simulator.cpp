#include "simulator.h" // Lowercase include
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>

PipelineSimulator::PipelineSimulator(int depth_config) 
    : D(depth_config), clock(0), retired_count(0), branch_stall(false) {
    
    // Frequencies based on D configuration [cite: 347-348]
    if (D == 1) frequency = 1.0;
    else if (D == 2) frequency = 1.2;
    else if (D == 3) frequency = 1.7;
    else if (D == 4) frequency = 1.8;
    else frequency = 1.0;

    for(int i = 0; i < 6; ++i) type_counts[i] = 0;
}

void PipelineSimulator::run(const std::string& trace_file, long long start_inst, long long inst_count) {
    std::ifstream file(trace_file);
    if (!file.is_open()) return;

    std::string line;
    long long lines_read = 0;
    
    // Skip to the starting instruction [cite: 312-314]
    while (lines_read < start_inst - 1 && std::getline(file, line)) {
        lines_read++;
    }

    // Main simulation loop [cite: 354]
    while (retired_count < inst_count) {
        int_busy = fp_busy = br_busy = load_busy = store_busy = false; // Reset structural hazards [cite: 327-332]

        processPipeline();

        // Fetch up to 2 instructions (2-wide superscalar) [cite: 299]
        for (int i = 0; i < 2 && !branch_stall && retired_count + window.size() < inst_count; ++i) {
            if (std::getline(file, line)) {
                std::stringstream ss(line);
                std::string part;
                uint64_t pc; int type;
                std::vector<uint64_t> deps;

                if (std::getline(ss, part, ',')) pc = std::stoull(part, nullptr, 16);
                if (std::getline(ss, part, ',')) type = std::stoi(part);
                while (std::getline(ss, part, ',')) {
                    if (!part.empty()) deps.push_back(std::stoull(part, nullptr, 16));
                }

                window.emplace_back(pc, type, deps);
            }
        }
        clock++;
    }
}

void PipelineSimulator::processPipeline() {
    for (auto it = window.begin(); it != window.end(); ) {
        it->cycles_in_stage++;

        if (it->stage == Stage::WB) { // [cite: 298, 355]
            type_counts[it->type]++;
            retired_count++;
            it = window.erase(it); 
            continue;
        }

        if (it->stage == Stage::MEM) { // [cite: 297]
            int mem_lat = (it->type == 4 && (D == 3 || D == 4)) ? 3 : 1; // Load latency [cite: 344-345]
            if (it->cycles_in_stage >= mem_lat) {
                it->stage = Stage::WB; it->cycles_in_stage = 0;
                last_done_cycle[it->pc] = clock; 
            }
        } else if (it->stage == Stage::EX) { // [cite: 296]
            int ex_lat = (it->type == 2 && (D == 2 || D == 4)) ? 2 : 1; // FP latency [cite: 343-345]
            if (it->cycles_in_stage >= ex_lat) {
                if (it->type == 3) branch_stall = false; 
                it->stage = Stage::MEM; it->cycles_in_stage = 0;
                if (it->type <= 3) last_done_cycle[it->pc] = clock; 
            }
        } else if (it->stage == Stage::ID) { // [cite: 295]
            if (canMoveToEX(*it)) {
                it->stage = Stage::EX; it->cycles_in_stage = 0;
                if (it->type == 3) branch_stall = true; // Branch delays fetch [cite: 308-309, 334]
            }
        } else if (it->stage == Stage::IF) { // [cite: 294]
            it->stage = Stage::ID; it->cycles_in_stage = 0;
        }
        ++it;
    }
}

bool PipelineSimulator::canMoveToEX(const Instruction& inst) {
    // Check Data Hazards [cite: 335-338]
    for (auto dep : inst.dep_pcs) {
        if (last_done_cycle.find(dep) == last_done_cycle.end() || last_done_cycle[dep] >= clock) 
            return false;
    }
    // Check Structural Hazards [cite: 327-332]
    if (inst.type == 1 && int_busy) return false;
    if (inst.type == 2 && fp_busy) return false;
    if (inst.type == 3 && br_busy) return false;
    
    if (inst.type == 1) int_busy = true;
    else if (inst.type == 2) fp_busy = true;
    else if (inst.type == 3) br_busy = true;
    return true;
}

void PipelineSimulator::printStats() const {
    double total = retired_count;
    // Format: Execution Time (cycles), %int, %FP, %branch, %load, %store
    std::cout << clock << ", " 
              << std::fixed << std::setprecision(2)
              << (type_counts[1]/total)*100.0 << ", " 
              << (type_counts[2]/total)*100.0 << ", "
              << (type_counts[3]/total)*100.0 << ", " 
              << (type_counts[4]/total)*100.0 << ", "
              << (type_counts[5]/total)*100.0 << std::endl;
}