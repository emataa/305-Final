#include "simulator.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstdlib>

// constructor: sets up the pipeline depth and maps it to frequency
PipelineSimulator::PipelineSimulator(int depth_config)
    : D(depth_config), clock(0), retired_count(0), fetched_count(0),
      next_seq(0), branch_stall(false), total_trace_lines(0)
{
    width = 2; // fixed 2-wide superscalar for this sim

    // frequency mapping for each depth d level
    if (D == 1)
        frequency = 1.0;
    else if (D == 2)
        frequency = 1.2;
    else if (D == 3)
        frequency = 1.7;
    else if (D == 4)
        frequency = 1.8;
    else
        frequency = 1.0;

    // init type counters to zero
    for (int i = 0; i < 6; ++i)
        type_counts[i] = 0;
}

// reset all per-run simulator state
void PipelineSimulator::resetState()
{
    clock = 0;
    retired_count = 0;
    fetched_count = 0;
    next_seq = 0;
    branch_stall = false;
    last_done_cycle.clear();

    for (int i = 0; i < 6; ++i)
        type_counts[i] = 0;

    for (int i = 0; i < 2; ++i)
    {
        if_stage[i] = PipeSlot();
        id_stage[i] = PipeSlot();
        ex_stage[i] = PipeSlot();
        mem_stage[i] = PipeSlot();
        wb_stage[i] = PipeSlot();
    }
}

// keeps valid instructions packed to the left so slot 0 is always older than slot 1
void PipelineSimulator::compactStage(PipeSlot stage[2])
{
    if (!stage[0].valid && stage[1].valid)
    {
        stage[0] = stage[1];
        stage[1] = PipeSlot();
    }
}

// checks whether a stage is empty
bool PipelineSimulator::stageEmpty(const PipeSlot stage[2]) const
{
    return !stage[0].valid && !stage[1].valid;
}

// checks whether the whole pipeline is empty
bool PipelineSimulator::pipelineEmpty() const
{
    return stageEmpty(if_stage) &&
           stageEmpty(id_stage) &&
           stageEmpty(ex_stage) &&
           stageEmpty(mem_stage) &&
           stageEmpty(wb_stage);
}

// latency helper for EX
int PipelineSimulator::getEXLatency(const Instruction &inst) const
{
    // floating point takes 2 cycles in ex if d is 2 or 4
    if (inst.type == 2 && (D == 2 || D == 4))
        return 2;

    return 1;
}

// latency helper for MEM
int PipelineSimulator::getMEMLatency(const Instruction &inst) const
{
    // loads take 3 cycles in mem if d is 3 or 4
    if (inst.type == 4 && (D == 3 || D == 4))
        return 3;

    return 1;
}

// dependency check
bool PipelineSimulator::depsReady(const Instruction &inst) const
{
    // IMPORTANT:
    // if a dependency PC isn't found, that means the producer is outside this replication,
    // so the dependence should be ignored and treated as ready
    for (size_t i = 0; i < inst.dep_pcs.size(); ++i)
    {
        uint64_t dep = inst.dep_pcs[i];
        auto found = last_done_cycle.find(dep);

        if (found == last_done_cycle.end())
            continue; // ignore dependences before the replication start

        // producer is ready if it completed in this cycle or earlier
        if (found->second > clock)
            return false;
    }

    return true;
}

// faster trace parsing than stringstream
bool PipelineSimulator::parseTraceLine(const std::string &line, Instruction &out_inst) const
{
    const char *p = line.c_str();
    char *end = nullptr;

    // parse pc (hex)
    uint64_t pc = std::strtoull(p, &end, 16);
    if (end == p || *end != ',')
        return false;
    p = end + 1;

    // parse type (decimal)
    int type = std::strtol(p, &end, 10);
    if (end == p)
        return false;

    std::vector<uint64_t> deps;

    // parse remaining dependency list
    while (*end != '\0')
    {
        if (*end == ',')
            ++end;

        if (*end == '\0')
            break;

        p = end;
        uint64_t dep = std::strtoull(p, &end, 16);
        if (end != p)
            deps.push_back(dep);
        else
            break;
    }

    out_inst = Instruction(pc, type, deps);
    return true;
}

// reads and parses the next instruction from the trace file
bool PipelineSimulator::readNextInstruction(std::ifstream &file, Instruction &out_inst)
{
    std::string line;

    if (!std::getline(file, line))
        return false;

    return parseTraceLine(line, out_inst);
}

// tries to load an existing sparse index from disk
bool PipelineSimulator::loadTraceIndex(const std::string &trace_file)
{
    std::ifstream idx(trace_file + ".idx");
    if (!idx.is_open())
        return false;

    long long stride = 0;
    long long line_count = 0;
    long long checkpoint_count = 0;

    idx >> stride >> line_count >> checkpoint_count;
    if (!idx.good() || stride != INDEX_STRIDE)
        return false;

    trace_checkpoints.clear();
    trace_checkpoints.reserve(checkpoint_count);
    total_trace_lines = line_count;

    for (long long i = 0; i < checkpoint_count; ++i)
    {
        long long off = 0;
        idx >> off;
        if (!idx.good())
            return false;
        trace_checkpoints.push_back((std::streamoff)off);
    }

    return true;
}

// builds a sparse index and saves it to disk for future runs
bool PipelineSimulator::buildTraceIndex(const std::string &trace_file)
{
    std::ifstream file(trace_file);
    if (!file.is_open())
        return false;

    trace_checkpoints.clear();
    total_trace_lines = 0;

    std::string line;
    while (true)
    {
        std::streamoff pos = (std::streamoff)file.tellg();

        if (!std::getline(file, line))
            break;

        // store a checkpoint for lines 1, 100001, 200001, ...
        if (total_trace_lines % INDEX_STRIDE == 0)
            trace_checkpoints.push_back(pos);

        total_trace_lines++;
    }

    std::ofstream idx(trace_file + ".idx");
    if (idx.is_open())
    {
        idx << INDEX_STRIDE << " "
            << total_trace_lines << " "
            << trace_checkpoints.size() << "\n";

        for (size_t i = 0; i < trace_checkpoints.size(); ++i)
            idx << (long long)trace_checkpoints[i] << "\n";
    }

    return total_trace_lines > 0;
}

// loads the index if it exists, otherwise builds it once
bool PipelineSimulator::ensureTraceIndex(const std::string &trace_file)
{
    if (loadTraceIndex(trace_file))
        return true;

    return buildTraceIndex(trace_file);
}

// jumps close to start_inst using the sparse index, then scans the small remainder
bool PipelineSimulator::seekToInstruction(std::ifstream &file, long long start_inst)
{
    if (start_inst <= 0 || start_inst > total_trace_lines)
        return false;

    long long target_zero_based = start_inst - 1;
    long long checkpoint_id = target_zero_based / INDEX_STRIDE;
    long long lines_to_skip = target_zero_based % INDEX_STRIDE;

    if (checkpoint_id < 0 || checkpoint_id >= (long long)trace_checkpoints.size())
        return false;

    file.clear();
    file.seekg(trace_checkpoints[checkpoint_id]);

    std::string dummy;
    while (lines_to_skip > 0 && std::getline(file, dummy))
        lines_to_skip--;

    return lines_to_skip == 0;
}

// main execution loop: parses the trace and runs the clock
void PipelineSimulator::run(const std::string &trace_file, long long start_inst, long long inst_count)
{
    current_trace = trace_file;
    current_start = start_inst;
    current_count = inst_count;

    resetState();

    if (!ensureTraceIndex(trace_file))
    {
        std::cerr << "error: could not build/load trace index for " << trace_file << std::endl;
        return;
    }

    std::ifstream file(trace_file);
    if (!file.is_open())
    {
        std::cerr << "error: could not open trace file " << trace_file << std::endl;
        return;
    }

    if (!seekToInstruction(file, start_inst))
    {
        std::cerr << "error: invalid start instruction " << start_inst << std::endl;
        return;
    }

    // simulate until the requested number of instructions retire
    while (retired_count < inst_count)
    {
        // pack all stages left before this cycle
        compactStage(if_stage);
        compactStage(id_stage);
        compactStage(ex_stage);
        compactStage(mem_stage);
        compactStage(wb_stage);

        // structural hazards reset every cycle
        int_busy = fp_busy = br_busy = load_busy = store_busy = false;

        bool branch_resolved_this_cycle = false;

        // ------------------------------------------------------------
        // 1) WB -> RETIRE
        // ------------------------------------------------------------
        for (int i = 0; i < 2; ++i)
        {
            if (wb_stage[i].valid)
            {
                type_counts[wb_stage[i].inst.type]++;
                retired_count++;
                wb_stage[i] = PipeSlot();
            }
        }
        compactStage(wb_stage);

        // ------------------------------------------------------------
        // 2) MEM -> WB
        // ------------------------------------------------------------
        for (int i = 0; i < 2; ++i)
        {
            if (!mem_stage[i].valid)
                continue;

            // if an older instruction in MEM can't leave yet, younger one can't bypass it
            if (i == 1 && mem_stage[0].valid)
            {
                int older_mem_lat = getMEMLatency(mem_stage[0].inst);
                if (mem_stage[0].inst.cycles_in_stage + 1 < older_mem_lat)
                    break;
            }

            mem_stage[i].inst.cycles_in_stage++;

            int mem_lat = getMEMLatency(mem_stage[i].inst);
            if (mem_stage[i].inst.cycles_in_stage < mem_lat)
                continue;

            // move to WB if there is room
            int wb_dest = -1;
            if (!wb_stage[0].valid)
                wb_dest = 0;
            else if (!wb_stage[1].valid)
                wb_dest = 1;

            if (wb_dest == -1)
                continue;

            // load/store dependences are satisfied after MEM finishes
            if (mem_stage[i].inst.type == 4 || mem_stage[i].inst.type == 5)
                last_done_cycle[mem_stage[i].inst.pc] = clock;

            mem_stage[i].inst.stage = Stage::WB;
            mem_stage[i].inst.cycles_in_stage = 0;
            wb_stage[wb_dest] = mem_stage[i];
            mem_stage[i] = PipeSlot();

            compactStage(mem_stage);
            compactStage(wb_stage);

            // if we moved slot 0 out, restart slot ordering logic
            if (i == 0 && mem_stage[0].valid == false && mem_stage[1].valid)
                compactStage(mem_stage);
        }

        compactStage(mem_stage);
        compactStage(wb_stage);

        // ------------------------------------------------------------
        // 3) EX -> MEM
        // ------------------------------------------------------------
        bool mem_load_used_this_cycle = false;
        bool mem_store_used_this_cycle = false;

        for (int i = 0; i < 2; ++i)
        {
            if (!ex_stage[i].valid)
                continue;

            // if an older instruction in EX can't leave yet, younger one can't bypass it
            if (i == 1 && ex_stage[0].valid)
            {
                int older_ex_lat = getEXLatency(ex_stage[0].inst);
                if (ex_stage[0].inst.cycles_in_stage + 1 < older_ex_lat)
                    break;
            }

            ex_stage[i].inst.cycles_in_stage++;

            int ex_lat = getEXLatency(ex_stage[i].inst);
            if (ex_stage[i].inst.cycles_in_stage < ex_lat)
                continue;

            // find MEM destination
            int mem_dest = -1;
            if (!mem_stage[0].valid)
                mem_dest = 0;
            else if (!mem_stage[1].valid)
                mem_dest = 1;

            if (mem_dest == -1)
                continue;

            // only one load may enter MEM per cycle, and only one store may enter MEM per cycle
            if (ex_stage[i].inst.type == 4)
            {
                if (mem_load_used_this_cycle)
                    continue;
            }
            else if (ex_stage[i].inst.type == 5)
            {
                if (mem_store_used_this_cycle)
                    continue;
            }

            // int/fp/branch dependences are satisfied after EX finishes
            if (ex_stage[i].inst.type >= 1 && ex_stage[i].inst.type <= 3)
                last_done_cycle[ex_stage[i].inst.pc] = clock;

            // branch resolves now, but fetch resumes next cycle
            if (ex_stage[i].inst.type == 3)
                branch_resolved_this_cycle = true;

            if (ex_stage[i].inst.type == 4)
                mem_load_used_this_cycle = true;
            else if (ex_stage[i].inst.type == 5)
                mem_store_used_this_cycle = true;

            ex_stage[i].inst.stage = Stage::MEM;
            ex_stage[i].inst.cycles_in_stage = 0;
            mem_stage[mem_dest] = ex_stage[i];
            ex_stage[i] = PipeSlot();

            compactStage(ex_stage);
            compactStage(mem_stage);

            if (i == 0 && ex_stage[0].valid == false && ex_stage[1].valid)
                compactStage(ex_stage);
        }

        compactStage(ex_stage);
        compactStage(mem_stage);

        // ------------------------------------------------------------
        // 4) ID -> EX
        // in-order rule: if the first instruction in ID stalls, the second also stalls
        // ------------------------------------------------------------
        for (int i = 0; i < 2; ++i)
        {
            if (!id_stage[i].valid)
                continue;

            if (i == 1 && id_stage[0].valid)
            {
                // if older one is still sitting in ID, younger one cannot bypass
                break;
            }

            int ex_dest = -1;
            if (!ex_stage[0].valid)
                ex_dest = 0;
            else if (!ex_stage[1].valid)
                ex_dest = 1;

            if (ex_dest == -1)
                break;

            if (!depsReady(id_stage[i].inst))
                break;

            // one INT ALU, one FP unit, one BR unit per cycle
            if (id_stage[i].inst.type == 1)
            {
                if (int_busy)
                    break;
                int_busy = true;
            }
            else if (id_stage[i].inst.type == 2)
            {
                if (fp_busy)
                    break;
                fp_busy = true;
            }
            else if (id_stage[i].inst.type == 3)
            {
                if (br_busy)
                    break;
                br_busy = true;
            }

            id_stage[i].inst.stage = Stage::EX;
            id_stage[i].inst.cycles_in_stage = 0;
            ex_stage[ex_dest] = id_stage[i];

            // branch blocks fetch immediately in this cycle
            if (id_stage[i].inst.type == 3)
                branch_stall = true;

            id_stage[i] = PipeSlot();

            compactStage(id_stage);
            compactStage(ex_stage);
        }

        compactStage(id_stage);
        compactStage(ex_stage);

        // ------------------------------------------------------------
        // 5) IF -> ID
        // also in-order: younger IF instruction cannot bypass older IF instruction
        // ------------------------------------------------------------
        for (int i = 0; i < 2; ++i)
        {
            if (!if_stage[i].valid)
                continue;

            if (i == 1 && if_stage[0].valid)
                break;

            int id_dest = -1;
            if (!id_stage[0].valid)
                id_dest = 0;
            else if (!id_stage[1].valid)
                id_dest = 1;

            if (id_dest == -1)
                break;

            if_stage[i].inst.stage = Stage::ID;
            if_stage[i].inst.cycles_in_stage = 0;
            id_stage[id_dest] = if_stage[i];
            if_stage[i] = PipeSlot();

            compactStage(if_stage);
            compactStage(id_stage);
        }

        compactStage(if_stage);
        compactStage(id_stage);

        // ------------------------------------------------------------
        // 6) FETCH
        // fetch is blocked while a branch is unresolved in EX
        // ------------------------------------------------------------
        if (!branch_stall)
        {
            for (int i = 0; i < width && fetched_count < current_count; ++i)
            {
                int if_dest = -1;
                if (!if_stage[0].valid)
                    if_dest = 0;
                else if (!if_stage[1].valid)
                    if_dest = 1;

                if (if_dest == -1)
                    break;

                Instruction next_inst(0, 1, std::vector<uint64_t>());
                if (!readNextInstruction(file, next_inst))
                    break;

                next_inst.stage = Stage::IF;
                next_inst.cycles_in_stage = 0;

                if_stage[if_dest].valid = true;
                if_stage[if_dest].seq = next_seq++;
                if_stage[if_dest].inst = next_inst;
                fetched_count++;
            }
        }

        compactStage(if_stage);

        // if a branch finished EX in this cycle, fetch resumes in the next cycle
        if (branch_resolved_this_cycle)
            branch_stall = false;

        // tick the master clock
        clock++;
    }
}

// prints the final results in a clean format
void PipelineSimulator::printStats() const
{
    double total = retired_count;

    // cycles / GHz gives nanoseconds, so divide by 1e6 to get milliseconds
    double time_ms = (frequency > 0.0) ? (clock / (frequency * 1000000.0)) : 0.0;

    std::cout << "\n>>> SIMULATION SETUP <<<" << std::endl;
    std::cout << " Trace File        : " << current_trace << std::endl;
    std::cout << " Pipeline Config   : " << width << "-wide, Depth=" << D
              << " (" << frequency << " GHz)" << std::endl;
    std::cout << " Start Instruction : " << current_start << std::endl;
    std::cout << " Total Simulated   : " << current_count << std::endl;

    std::cout << "\n>>> PERFORMANCE METRICS <<<" << std::endl;
    std::cout << std::left << std::setw(20) << " Total Cycles" << " : " << clock << std::endl;
    std::cout << std::left << std::setw(20) << " Execution Time" << " : "
              << std::fixed << std::setprecision(6) << time_ms << " ms" << std::endl;
    std::cout << std::left << std::setw(20) << " IPC" << " : "
              << std::setprecision(3) << (clock > 0 ? total / clock : 0.0) << std::endl;

    std::cout << "\n>>> INSTRUCTION MIX <<<" << std::endl;
    const char *types[] = {"Integer", "Floating Point", "Branch", "Load", "Store"};
    for (int i = 0; i < 5; i++)
    {
        double pct = (total > 0) ? (type_counts[i + 1] / total) * 100.0 : 0.0;
        std::cout << " - " << std::left << std::setw(15) << types[i]
                  << " : " << std::fixed << std::setprecision(2) << pct << "%" << std::endl;
    }

    double int_pct = (total > 0) ? (type_counts[1] / total) * 100.0 : 0.0;
    double fp_pct = (total > 0) ? (type_counts[2] / total) * 100.0 : 0.0;
    double br_pct = (total > 0) ? (type_counts[3] / total) * 100.0 : 0.0;
    double ld_pct = (total > 0) ? (type_counts[4] / total) * 100.0 : 0.0;
    double st_pct = (total > 0) ? (type_counts[5] / total) * 100.0 : 0.0;

    // reset the precision for the CSV so execution time keeps full detail
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "\nCSV_LOG: "
              << clock << ","
              << time_ms << ","
              << int_pct << ","
              << fp_pct << ","
              << br_pct << ","
              << ld_pct << ","
              << st_pct << std::endl;
}