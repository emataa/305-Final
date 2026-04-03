#include <vector>
#include <deque>
#include <unordered_map>

struct Instruction {
    unsigned long pc;
    int type; // 1-5
    std::vector<unsigned long> dep_pcs; // The PCs we depend on
    
    int cycles_left_in_stage; // For D2, D3, D4
    bool is_stalled = false;

    // Trackers for the final report
    int start_cycle;
    int end_cycle;
};

class Pipeline {
public:
    // Stages as deques for 2-wide superscalar
    std::deque<Instruction> IF, ID, EX, MEM, WB;
    
    // The "Scoreboard": PC -> Cycle it will be ready
    std::unordered_map<unsigned long, int> ready_table;

    void tick(); // Person B's masterpiece
};