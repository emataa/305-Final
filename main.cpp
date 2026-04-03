#include "simulator.h"
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 5) {
        std::cerr << "usage: ./proj trace_file start_inst inst_count d" << std::endl;
        return 1;
    }

    // parse the command line inputs
    std::string trace = argv[1];           // name of the trace file (e.g., srv_0)
    long long start = std::stoll(argv[2]); // index of the first instruction to simulate
    long long count = std::stoll(argv[3]); // total number of instructions to run
    int D = std::stoi(argv[4]);            // pipeline depth configuration (1, 2, 3, or 4)

    // initialize the simulator with the chosen depth setting
    PipelineSimulator sim(D);

    // start the simulation loop
    sim.run(trace, start, count);

    // output the final cycles and instruction breakdown
    sim.printStats();

    return 0;
}