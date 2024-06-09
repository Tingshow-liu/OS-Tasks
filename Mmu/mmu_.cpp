#include <array>
#include <vector>
#include <cstdint>  // for uint32_t
#include <iostream>
#include <getopt.h>
#include <string>
#include <fstream>
#include <sstream>
#include <queue>
#include <climits>  // For INT_MAX
#include <limits>   // For UINT_MAX and other limits
using namespace std;

// Some global variables
constexpr int pageTableSize = 64;  // Max size of each page table = 64
int currVmaNum;  // Recore the # of VMAs of the current process
int occupiedFrame = 0;  // Number of frames used
bool segv;  // Whether there's a segv error
int numFrames = 0;  // Number of frames
int idx = 0;
int randNumCnt = 0;  // To load random numbers
vector<int> randvals;  // To load random numbers
int ofs = 0;  // To get the random number
bool workingSet = false;  // Whether the pager is working-set
// -oOPFS & variables
bool O_flag = false;
bool P_flag = false;
bool F_flag = false;
bool S_flag = false;
// For the final summary (S flag)
long inst_count = 0;
long ctx_switches = 0;  
long process_exits = 0;
long totalMaps = 0;
long totalUnmaps = 0;
long totalIns = 0;
long totalOuts = 0;
long totalFins = 0;
long totalFouts = 0;
long totalZeros = 0;
long totalSegv = 0;
long totalSegprot = 0;
long totalRead = 0;
long totalWrite = 0;
long totalExit = 0;

// PTE strucutre (32 bits)
struct Pte_t{
    uint32_t PRESENT: 1;  // PRESENT occupies 1 bit of space in the structure
    uint32_t REFERENCED: 1;
    uint32_t MODIFIED: 1;
    uint32_t WRITE_PROTECT: 1;
    uint32_t FILE_MAPPED : 1;  // Not mentioned in the assignment requirement
    uint32_t PAGEDOUT: 1;
    uint32_t FRAMENUMBER: 7;
    uint32_t VALID_VMA: 1;  // Whether the page is part of one of the VMAs
    uint32_t PRE_REFERENCED: 1;  // Since the REFERENCED bit will be reset for working-set pager every time, use this bit the record its previous referenced state to print out the final summary correctly
    // uint32_t REMAIN: 20;  // REMAIN occupies 20 bits of space in the structure: TBA
    // constructor
    Pte_t() {
        PRESENT = 0;
        REFERENCED = 0;
        MODIFIED = 0;
        WRITE_PROTECT = 0;
        FILE_MAPPED = 0;
        PAGEDOUT = 0;
        FRAMENUMBER = 0;
        VALID_VMA = 0;
        PRE_REFERENCED = 0;
    }
};

struct pstats {
    long maps = 0;
    long unmaps = 0;
    long ins = 0;
    long outs = 0;
    long fins = 0;
    long fouts = 0;
    long zeros = 0;
    long segv = 0;
    long segprot = 0;
};

class Vma {
    public:
        int startVpage = 0;
        int endVpage = 0;
        int vpageNum = 0;
        bool writeProtected = false;
        bool fileMapped = false;
        // Constructor
        Vma(int start_vpage, int end_vpage, bool write_protected, bool file_mapped):
        startVpage(start_vpage), endVpage(end_vpage), vpageNum(endVpage - startVpage + 1), writeProtected(write_protected), fileMapped(file_mapped) {}
};

// Process class: TBA
class Process {
    public:
        int processId;
        int vmaNum = 0;
        vector<Vma> vmaTable;
        Pte_t pageTable[pageTableSize];  // The process's page table:
        bool exit = false;  // Whether the process is about to complete (exit)
        pstats* stats;
        // Constructor
        Process(int id, int vma_num): processId(id), vmaNum(vma_num) {
            stats = new pstats;  // Initialize the pstats struct in process
        }
};
vector<Process*> processTable;  // Stores pointers to all processes
Process* currProc;  // Process Id of the current process switched to (current_process)
Pte_t* currPageTable;  // Page table of the current process

// frame structure: TBA
struct Frame_t {
    int f_id;  // The id of this frame
    bool inUse = false;  // If the frame is currently in use
    Process* process;  // ID of the process that owns the frame, -1 if unused
    int vPage = -1;  // Virtual page number mapped to this frame, -1 if unused
    unsigned int age;  // For the aging pager
    int time_last_used;   // For the working-set pager
    bool canReplace = false;  // Whether the frame can be replaced for working-set pager
    Frame_t(int id): f_id(id) {}
};
vector<Frame_t> frameTable;  // The frame table that stores all frames
deque<Frame_t*> freeFrames;  // The deque to manage all free frames

// Cost of each instruction
struct InstrCost {
    long maps = 350;
    long unmaps = 410;
    long ins = 3200;
    long outs = 2750;
    long fins = 2350;
    long fouts = 2800;
    long zeros = 150;
    long segv = 440;
    long segprot = 410;
    long ctx_switch = 130;
    long exit = 1230;
    long read = 1;
    long write = 1;
};

// Initialize frameTable and freeFrames
void create_frames(int frameNum) {
    for (int i = 0; i < frameNum; i++) {
        Frame_t frame(i);
        frameTable.push_back(frame);  // Add frame to the table
    }
    // Now, store pointers to the objects in frameTable in freeFrames
    for (int i = 0; i < frameNum; i++) {
        freeFrames.push_back(&frameTable[i]);  // Store address of frames in frameTable
    }
}

// Address exiting processes
void exit_handler(Process* exitProc) {
    cout << "EXIT current process " << exitProc->processId << endl;
    // Unmap all mapped pages of the process from frames
    for (int i = 0; i < pageTableSize; i++) {
        exitProc->pageTable[i].PAGEDOUT = 0;  // First reset the PAGEDOUT of all pages of the exit process
        if (exitProc->pageTable[i].PRESENT) {
            exitProc->pageTable[i].PRESENT = 0;
            if (O_flag) { cout << " UNMAP " << exitProc->processId << ":" << i << endl; }
            // If pte is modified/ dirty (written to) and filemapped, need to write it back to its file (If the process if not filemapped, no need to write back to swap space since the process is exiting)
            if (exitProc->pageTable[i].MODIFIED && exitProc->pageTable[i].FILE_MAPPED) {
                if (O_flag) { cout << " FOUT" << endl; }
                exitProc->stats->fouts++;  // Update pstats
            }
            exitProc->stats->unmaps++;  // Need this line???
            // Free the frame mapped to this page of the exit process and add it back freeFrames
            Frame_t& frameToFree = frameTable[exitProc->pageTable[i].FRAMENUMBER];
            frameToFree.process = nullptr;
            frameToFree.vPage = -1;
            frameToFree.inUse = false;
            freeFrames.push_back(&frameToFree);
        }
    }
}

// Unmap a frame from a page (for instructions "r" and "w")
void unmap_frame_page(Frame_t* frame) {
    Process* proc = frame->process;  // Current process mapped to this frame
    Pte_t* pte = &proc->pageTable[frame->vPage];  // Current page mapped to this frame
    // The process is not exiting!
    if (O_flag) { cout << " UNMAP " << proc->processId << ":" << frame->vPage << endl; }
        proc->stats->unmaps++;  // Update pstats

    if (pte->MODIFIED) {  // It pte is modified/ dirty -> go to swap space (OUT) or the mappedfile (FOUT)
        if (pte->FILE_MAPPED) {  // Go to its mappedfile
            if (O_flag) { cout << " FOUT" << endl; }
            proc->stats->fouts++;  // Update pstats
        } else {  // Go to swap space
            if (O_flag) { cout << " OUT" << endl; }
            proc->stats->outs++;  // Update pstats
            pte->PAGEDOUT = 1;  // The page is swapped out
        }
        pte->MODIFIED = 0;  // Reset the MODIFIED flag
    // If the page is not modified before, unmap the page and the frame directly
    } 
    frame->process = nullptr;
    frame->vPage = -1;
    frame->inUse = false;
    freeFrames.push_back(frame);  // The used frame has to be returned to the free pool 
    pte->PRESENT = 0;  // The page now doesn't present in any frame 
}

// Map a frame to a page
void map_frame_page(Frame_t* frame, Process* proc, int vpage) {
    Pte_t* pte = &proc->pageTable[vpage];
    pte->PRESENT = 1;
    pte->FRAMENUMBER = frame->f_id;
    if (pte->FILE_MAPPED) {
        if (O_flag) {cout << " FIN" << endl; }  // If the page is filemapped, load data from file 
        proc->stats->fins++;  // Update pstats
    } else {
        if (pte->PAGEDOUT) {
            if (O_flag) {cout << " IN" << endl; }  // If the page is not filemapped and was move to the swap space ("OUT" before), load data from swap space to the frame again
            proc->stats->ins++;  // Update pstats
        } else {  // The page was never swapped out and not filemapped
            if (O_flag) { cout << " ZERO" << endl; }
            proc->stats->zeros++;  // Update pstats
        }
    }
    frame->inUse = true;
    frame->process = proc;
    frame->vPage = vpage;
    frame->age = 0;
    freeFrames.pop_front();
    if (O_flag) {cout << " MAP " << frame->f_id << endl; }
    proc->stats->maps++;
}

// Maintain the instruction table
struct Instructions {
    char operation;  // c, r, w, e
    int vpage;
    // Constructor
    Instructions(char type, int id): operation(type), vpage(id) {}
    // Get the next process
};
// Use a queue to store all instructions read
queue<Instructions> instructions; 

// Get next instruction from the queue
bool get_next_instruction(char& operation, int& vpage) {
    if (!instructions.empty()) {
        Instructions currInstr = instructions.front();  // Get the next instruction
        instructions.pop();  // Remove the next instruction from the queue
        operation = currInstr.operation;
        vpage = currInstr.vpage;
        return true;
    }
    return false;
}

// Virtual base class of all pager algorithms
class Pager{
    public:
        // virtual functions
        virtual Frame_t* select_victim_frame() = 0;
};

// FIFO pager
class FIFO: public Pager {
    private:
        vector<Frame_t>& frameTable;
        int hand = 0;
    public:
        FIFO(vector<Frame_t>& frameTable): frameTable(frameTable) {}  // constructor
        Frame_t* select_victim_frame() override {
            // Use the frame at the current "hand" position
            Frame_t* victimFrame = &frameTable[hand];
            // Move "hand" to the next frame (with wraparound)
            hand = (hand + 1) % frameTable.size();
            return victimFrame;
        }
};

// Clock pager
class Clock: public Pager {
    private:
        vector<Frame_t>& frameTable;
        int hand = 0;
    public:
        Clock(vector<Frame_t>& frameTable): frameTable(frameTable) {}  // constructor
        Frame_t* select_victim_frame() override {
            while (true) {
                Frame_t* victimFrame = &frameTable[hand];
                // Check if the page is referenced recently, if yes, give it another chance
                if (!victimFrame->process->pageTable[victimFrame->vPage].REFERENCED) {
                    hand = (hand + 1) % frameTable.size();
                    return victimFrame;
                } else {
                    victimFrame->process->pageTable[victimFrame->vPage].REFERENCED = 0;
                    hand = (hand + 1) % frameTable.size();
                }
            }
        }
};

// Define the function used to get a random number
int myrandom(vector<int> randNumbers) { 
    int randNum = randNumbers[ofs] % numFrames;
    if (ofs+1 < randNumCnt) {
        ofs++;
    } else {
        ofs = 0;  // Wrap around when running out of numbers in the file/array
    }
    return randNum;
}
// Random pager
class Random: public Pager {
    private: 
        vector<Frame_t>& frameTable;
    public:
        Random(vector<Frame_t>& frameTable): frameTable(frameTable) {}
        Frame_t* select_victim_frame() override {
            int randNum = myrandom(randvals);
            Frame_t* victimFrame = &frameTable[randNum];
            return victimFrame;
        }
};

// Define the daemon function to reset the REFERENCED bits for all pages mapped to a frame every 48 instructions
static int instrCounter = 0;
void daemon(vector<Frame_t>& frameTable) {
    if (instrCounter >= 48) {
        for (vector<Frame_t>::iterator frame = frameTable.begin(); frame != frameTable.end(); frame++) {
            if (frame->inUse && frame->process && frame->vPage != -1) {
                frame->process->pageTable[frame->vPage].REFERENCED = 0;
            }
        }
        instrCounter = 0;  // Reset the global counter
    }
}
// NRU (ESC) pager
class NRU: public Pager {
    private: 
        vector<Frame_t>& frameTable;
        int hand = 0;
    public:
        NRU(vector<Frame_t>& frameTable): frameTable(frameTable) {}
        Frame_t* select_victim_frame() override {
            int classToReplace = 4;  // First set this as a value larger than 3 (since 3 is the possible largest class)
            Frame_t* victimFrame = nullptr;

            for (int i = 0; i < frameTable.size(); i++) {
                int idx = (hand + i) % frameTable.size();  // Circular scan
                Frame_t& currFrame = frameTable[idx];
                // Get the PTE of mapped to this frame
                Pte_t& currPte = currFrame.process->pageTable[currFrame.vPage];
                int frameClass = 2 * currPte.REFERENCED + currPte.MODIFIED;
                if (frameClass < classToReplace) {
                    victimFrame = &currFrame;
                    classToReplace = frameClass;
                    if (frameClass == 0) { break; }  // Stop when we find a class 0
                }
            }
            // Call daemon to reset REFERENCED bits if needed
            daemon(frameTable);  

            if (victimFrame != nullptr) {  // Update hand for the next call after a victimFrame is correctly selected
                hand = (victimFrame->f_id + 1) % frameTable.size();
            }
            return victimFrame;
        }
};

// Aging pager
class Aging: public Pager {
    private: 
        vector<Frame_t>& frameTable;
        int hand = 0;
    public:
        Aging(vector<Frame_t>& frameTable): frameTable(frameTable) {}
        Frame_t* select_victim_frame() override {
            for (int i = 0; i < frameTable.size(); i++) {
                int currIdx = (hand + i) % frameTable.size();
                Frame_t& currFrame = frameTable[currIdx];
                if (currFrame.inUse) {
                    currFrame.age >>= 1;
                    // If the page was referenced recently, set its leading bit to 1
                    if (currFrame.process->pageTable[currFrame.vPage].REFERENCED) {
                        currFrame.age |= 0x80000000;  // Set the MSB if the page was recently referenced
                        currFrame.process->pageTable[currFrame.vPage].REFERENCED = 0;  // Reset the referenced bit
                    }
                }
            }
            // Then find the victim frame
            // The youngest page will have the biggest age (bit value) and the oldest page to be unmapped will have the smallest age
            unsigned int minAge = UINT_MAX;  // The oldest page (vicitm frame) will have the min age (minAge = oldest page)
            Frame_t* victimFrame = nullptr;
            int victimIdx = hand;  // Start from the current hand position

            for (int i = 0; i < frameTable.size(); i++) {
                int currIdx = (hand + i) % frameTable.size();
                Frame_t& currFrame = frameTable[currIdx];

                // Check for victim frame
                if (currFrame.inUse && currFrame.age < minAge) {
                    minAge = currFrame.age;
                    victimFrame = &currFrame;
                    victimIdx = currIdx;
                }
            }
            // Update hand to the position right after the current victim frame
            hand = (victimIdx + 1) % frameTable.size();
            
            return victimFrame;
        }
};

// Working-set pager
int currentTime = 0;  // Used to calculate TAU (by instructions)
class WorkingSet: public Pager {
    private:
        vector<Frame_t>& frameTable;
        int hand = 0;
    public:
        WorkingSet(vector<Frame_t>& frameTable): frameTable(frameTable) {}
        Frame_t* select_victim_frame() override {
            static const int TAU = 49;  // Time criteria
            int oldestTime = INT_MAX;
            Frame_t* oldestFrame = nullptr;
            Frame_t* victimFrame = nullptr;

            for (int i = 0; i < frameTable.size(); i++) {
                Frame_t& currFrame = frameTable[(hand + i) % frameTable.size()];

                // Update the "time_last_used" of the frame based on whether it's referenced recently or its TAU
                // If the page is REFERENCED recently, set time_last_used to currentTime and reset its REFERENCED to 0
                if (currFrame.process && currFrame.process->pageTable[currFrame.vPage].REFERENCED) {
                    // Keep the previous state of the REFERENCED bit to print the correct "R" state at the end 
                    currFrame.process->pageTable[currFrame.vPage].PRE_REFERENCED = currFrame.process->pageTable[currFrame.vPage].REFERENCED;
                    currFrame.process->pageTable[currFrame.vPage].REFERENCED = 0;
                    currFrame.time_last_used = currentTime;
                }
                // Check eligibility after resetting the REFERENCED bits
                if (!currFrame.process->pageTable[currFrame.vPage].REFERENCED && currentTime - currFrame.time_last_used > TAU) {
                    currFrame.canReplace = true;
                } else {
                    currFrame.canReplace = false;
                }
                // Select the first frame that is eligible to be replaced
                if (currFrame.canReplace) {
                    victimFrame = &currFrame;
                    break;
                }
                // Track the oldest frame in case no eligible frame is found (The smaller the frame's time_last_used is, the older it is)
                if (currFrame.time_last_used < oldestTime) {
                    oldestTime = currFrame.time_last_used;
                    oldestFrame = &currFrame;
                }
            }
            // If no eligible frame is found, use the oldest frame
            if (!victimFrame) {
                victimFrame = oldestFrame;
            }
            // Update hand for the next round
            hand = (victimFrame->f_id + 1) % frameTable.size();

            return victimFrame;
        }
};

// First initialize a pager object 
Pager* pager;

// Get the next frame that should be mapped to the page after consulting the pagers
Frame_t* get_frame() {
    Frame_t* frame = nullptr;
    // If there are free frames
    if (!freeFrames.empty()) {
        frame = freeFrames.front();  // Get the first (oldest) free frame's memory address
        // freeFrames.pop_front();
    } else {  // There's no any free frame -> paging
        frame = pager->select_victim_frame();
    }
    return frame;
}

// Handle page fault: If the page is valid (belongs to a VMA), allocate a frame to it
void pagefault_handler(Pte_t* pte, vector<Vma>* vmaTable, int vpage) {
    // If the page is not valid or not confirmed valid (belongs to a VMA) before, check it
    if (!pte->VALID_VMA) {
        for (Vma& vma: *vmaTable) {  // Check whether the page belongs to a VMA and record it 
            if (vpage >= vma.startVpage && vpage <= vma.endVpage) {
                pte->VALID_VMA = 1;
                // Also update the page's WRITE_PROTECT and FILE_MAPPED variables too since it's valid
                if (vma.fileMapped) { pte->FILE_MAPPED = 1; }
                if (vma.writeProtected) { pte->WRITE_PROTECT = 1; }
                break;
            } 
        }
        // After checking and the page is not valid (doesn't belong to a VMA) -> a SEGV output line must be created
        if (!pte->VALID_VMA) {  
            segv = true;
            return;  // The page is invalid, return directly
        }
    }
    // If the page is (already) confirmed valid (belongs to a VMA) 
    Frame_t* frame = get_frame();  // Allocate or reclaim a frame
    if (frame->inUse) {
        unmap_frame_page(frame); 
    }
    map_frame_page(frame, currProc, vpage);
}

// Load random numbers into the vector (code from lab2)
vector<int> loadRandNumbers(const string& randNumFile) {  // randNumFile: "rfile"
    ifstream file(randNumFile);
    if (!file.is_open()) {
        cout << "Fail to open the random number file" << endl;
        exit(2);
    }
    int currNumber;
    if (!file >> currNumber) {
        cout << "Empty random number file" << endl;    
    }
    // 1st number is the # of random numbers in the file (pass it)
    file >> randNumCnt;
    // Add all the random numbers to the vector
    for (int i = 0; i < randNumCnt; i++) {
        file >> currNumber;
        randvals.push_back(currNumber);
    }
    // Close the file after loading all numbers to the vector
    file.close();
    return randvals;
}

// Print -o"OPFS" (O done)
// P
void pageTable_printer(const vector<Process*>& processTable) {
    for (vector<Process*>::const_iterator process = processTable.begin(); process != processTable.end(); process++) {
        printf("PT[%d]: ", (*process)->processId);
        // Print all pages from the page table of the process
        for (int i = 0; i < pageTableSize; i++) {
            // To access the pageTable of a Process object pointed to by the iterator is to first dereference the iterator to get the Process pointer and then use -> to access the pageTable
            Pte_t& pte = (*process)->pageTable[i];  
            if (pte.PRESENT) {
                // pte_t_size++;
                printf("%d:", i); // Page number
                if (workingSet) {
                    printf(pte.PRE_REFERENCED ? "R" : "-");
                } else {
                    printf(pte.REFERENCED ? "R" : "-");
                }
                
                printf(pte.MODIFIED ? "M" : "-");
                printf(pte.PAGEDOUT ? "S" : "-");
            } else {
                printf(pte.PAGEDOUT ? "#" : "*");
            }
            // Only add a space if it's not the last page
            if (i < pageTableSize - 1) {
                printf(" ");
            }
        }
        printf("\n");
    }
}
// F
void frameTable_printer(const std::vector<Frame_t>& frameTable) {
    printf("FT:");
    for (const Frame_t& frame : frameTable) {
        if (frame.inUse) {
            printf(" %d:%d", frame.process->processId, frame.vPage);
        } else {
            printf(" *");
        }
    }
    printf("\n");
}
// S
void summary_printer(const Process& proc) {
    // inst_count, ctx_switches, process_exits, cost, sizeof(pte_t))
    unsigned long long totalCost = 0;
    InstrCost* costs = new InstrCost;
    // Calculate the total count of each instruction for TOTALCOST
    totalMaps += proc.stats->maps;
    totalUnmaps += proc.stats->unmaps;
    totalIns += proc.stats->ins;
    totalOuts += proc.stats->outs;
    totalFins += proc.stats->fins;
    totalFouts += proc.stats->fouts;
    totalZeros += proc.stats->zeros;
    totalSegv += proc.stats->segv;
    totalSegprot += proc.stats->segprot;
    totalCost = totalCost + totalMaps*costs->maps + totalUnmaps*costs->unmaps + totalIns*costs->ins + totalOuts*costs->outs + totalFins*costs->fins 
                + totalFouts*costs->fouts + totalZeros*costs->zeros + totalSegv*costs->segv + totalSegprot*costs->segprot + totalRead*costs->read +
                totalWrite*costs->write + totalExit*costs->exit + ctx_switches*costs->ctx_switch;

    printf("PROC[%d]: U=%lu M=%lu I=%lu O=%lu FI=%lu FO=%lu Z=%lu SV=%lu SP=%lu\n",
        proc.processId,
        proc.stats->unmaps, proc.stats->maps, proc.stats->ins, proc.stats->outs,
        proc.stats->fins, proc.stats->fouts, proc.stats->zeros,
        proc.stats->segv, proc.stats->segprot);
    
    if (proc.processId == processTable.size() - 1) {
        printf("TOTALCOST %lu %lu %lu %llu %lu\n",
        inst_count, ctx_switches, process_exits, totalCost, sizeof(Pte_t));  // pte_t_size? 4? for the last field?
    }
}

int main(int argc, char *argv[]) {
    int opt;
    char algo = '\0';  // Selected paging algorithm
    string options;  // Optional output options
    string processFile, randFile;
    while ((opt = getopt(argc, argv, "f:a:o:")) != -1) {
        switch (opt) {
            case 'f':
                numFrames = stoi(optarg);  // Option argument
                break;
            case 'a':
                algo = optarg[0];
                break;
            case 'o':
                options = optarg;  
                for (char& c: options) {
                    switch (c) {
                        case 'O':
                            O_flag = true;
                            break;
                        case 'P':
                            P_flag = true;
                            break;
                        case 'F':
                            F_flag = true;
                            break;
                        case 'S':
                            S_flag = true;
                            break;
                        default:
                            cout << "Invalid option character: " << c << endl;
                            break;
                    }
                }
                break;
            default:
                return 1;
        }
    }
    // infile, rfile after the processing options (Process remaining arguments that are not options)
    if (optind < argc) {
        processFile = argv[optind++];
        if (optind < argc) {
            randFile = argv[optind];
        }
    }
    // Initialize freeFrames and frameTable
    create_frames(numFrames);
    // Initialize the pager (Default: FIFO)
    pager = new FIFO(frameTable);
    if (algo == 'f') { pager = new FIFO(frameTable); }
    if (algo == 'r') { pager = new Random(frameTable); }
    if (algo == 'c') { pager = new Clock(frameTable); }
    if (algo == 'e') { pager = new NRU(frameTable); }
    if (algo == 'a') { pager = new Aging(frameTable); }
    if (algo == 'w') { pager = new WorkingSet(frameTable); }

    // First read random file
    randvals = loadRandNumbers(randFile);
    // Read process file
    ifstream processFileStream(processFile);
    string line;
    // Skip initial comment lines: continue looping until finding a line that is not empty and does not start with '#'
    while (getline(processFileStream, line) && (line.empty() || line[0] == '#'));
    // This line contains the number of processes
    int processNum = stoi(line);
    // Process each process
    for (int p = 0; p < processNum; p++) {
        while (getline(processFileStream, line) && (line.empty() || line[0] == '#'));
        // # of VMAs in the proces
        currVmaNum = stoi(line);  // Read current process's VMA count and store it in "line"
        // First initialize a process
        Process* currP = new Process(p, currVmaNum);
        // Process VMAs in each process
        for (int v = 0; v < currVmaNum; v++) {
            getline(processFileStream, line);  // Read each line and store it in "line"
            istringstream iss(line);  // iss: treat a string object like a stream, so can extract values from line
            int start_vpage, end_vpage;
            bool write_protected, file_mapped;
            if (iss >> start_vpage >> end_vpage >> write_protected >> file_mapped) {
                Vma currVma = Vma(start_vpage, end_vpage, write_protected, file_mapped);
                currP->vmaTable.push_back(currVma);
            }
        }
        // Add process to the process table
        processTable.push_back(currP);
    }
    // Read and store all instructions into a queue
    while (getline(processFileStream, line)) {
        if (line.empty() || line[0] == '#' || line.find("####") != string::npos) {
            continue;
        }
        istringstream iss(line);
        char instrType;
        int instrValue;
        if (iss >> instrType >> instrValue) {
            Instructions currInstr(instrType, instrValue);
            instructions.push(currInstr);
        }
    }
    inst_count = instructions.size();

    // Simulation structure
    char operation;
    int vpage;
    while (get_next_instruction(operation, vpage)) {
        if (O_flag) { cout << idx << ": ==> " << operation << " " << vpage << endl; }
        idx++;
        instrCounter++;  // Increase instrCounter by 1 for the daemon function
        currentTime++;  // Increase currentTime by 1 
        switch (operation) {
            case 'c':
                currProc = processTable[vpage];  
                ctx_switches++;
                // currPageTable = currProc->pageTable;
                break;
            case 'e':
                currProc = processTable[vpage];  // exiting process
                currProc->exit = true;
                process_exits++;
                totalExit++;
                exit_handler(currProc);
                break;
            default:
                Pte_t* pte = &currProc->pageTable[vpage];  // Get the correct page from pageTable in process
                vector<Vma>* currVmaTable = &currProc->vmaTable;
                // updateTimeLastUsed(frameTable);  // Update the time_last_used variable for each frame (for working-set pager)
                if (!pte->PRESENT) {  // Handle page fault
                    pagefault_handler(pte, currVmaTable, vpage);  // Handle page fault error 
                    if (segv) {  // If it's not valid, print error message and continue to the next instruction
                        segv = false;
                        if (O_flag) { cout << " SEGV" << endl; }
                        currProc->stats->segv++;  // Update pstats
                        if (operation == 'r') { totalRead++; }  // Also update totalRead and totalWrite
                        if (operation == 'w') { totalWrite++; }
                        continue;  // Print an SEGV error message and continue to the next instruction
                    }
                    // Frame_t* newframe = get_frame();  // The page is valid (belongs to a VMA), so assign a frame to it
                } 
                if (operation == 'r') {
                    pte->REFERENCED = 1;
                    totalRead++;
                } else {  // operation == 'w'
                    pte->REFERENCED = 1;
                    if (pte->WRITE_PROTECT) {
                        if (O_flag) { cout << " SEGPROT" << endl; }
                        currProc->stats->segprot++;  // Update pstats
                    } else {
                        pte->MODIFIED = 1;  // The page is modified (written to)
                    }
                    totalWrite++;
                }
        }
    }
    if (P_flag) { pageTable_printer(processTable); }
    if (F_flag) { frameTable_printer(frameTable); }
    if (S_flag) {
        for (vector<Process*>::const_iterator process = processTable.begin(); process != processTable.end(); ++process) {
            summary_printer(**process);  // Dereference iterator to get Process* and then dereference again to pass Process to summary_printer
        }
    }
    return 0;
}