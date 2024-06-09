#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <queue>
#include <getopt.h>
#include <climits>
#include <algorithm>
using namespace std;

class IO_Operation {
    public:
        int operation_id;
        int arrivalTime;
        int startTime;
        int trackNumber;
        int endTime;
        // Constructor
        IO_Operation(int id, int arriveTime, int trackNum): operation_id(id), arrivalTime(arriveTime), trackNumber(trackNum) {}
};

// Global variables
vector<IO_Operation*> IO_list;  // Store all IO operations after parsing the input file
vector<IO_Operation*> complete_IOs;
// queue<IO_Operation*> IO_queue;  // Contains processes that arrive at current time
int currentTime = 0;
int operationCnt = 0;  // Current count (number) of the opeartion
int currPos = 0;  // Current position of the disk head
int direction = 1;  // Whether the head is moving forward or backward
bool isActiveIO = false;  // Whether there's any IO operation active at current time
IO_Operation* activeIO;  
bool V_flag = false;  
bool Q_flag = false;  
bool F_flag = false;  
// For the final summary
int total_time = 0;
int tot_movement = 0;
int ttlIoBusyTime = 0;
double io_utilization = 0; 
int ttl_turnaround = 0; 
double avg_turnaround = 0; 
int ttl_waittime = 0; 
double avg_waittime = 0;
int max_waittime = 0;


// Parent class of all schedulers
class Scheduler {
    public:
        // virtual functions
        virtual IO_Operation* get_next_request() = 0;
        virtual void enqueue(IO_Operation* op) = 0;
        virtual bool isEmpty() = 0;
};

// FIFO
class FIFO: public Scheduler {
    private:
        queue<IO_Operation*> IO_queue; 
    public:
        // Get the next IO request
        IO_Operation* get_next_request() override {
            if (!IO_queue.empty()) {
                IO_Operation* op = IO_queue.front();
                IO_queue.pop();
                return op;
            }
            return nullptr;
        }
        // Enqueue
        void enqueue(IO_Operation* op) override {
            IO_queue.push(op);
        }
        // Whether the queue is empty
        bool isEmpty() override {
            if (IO_queue.empty()) {
                return true;
            } else {
                return false;
            }
        }
};

// SSTF
class SSTF: public Scheduler {
    private:
        vector<IO_Operation*> IO_queue;  // Use vector for easier traversal
    public:
        // Get the next IO request
        IO_Operation* get_next_request() override {
            IO_Operation* next_request = nullptr;
            int nearest_dis = INT_MAX;
            int idx_to_erase = 0;
            // Select the IO request that is nearest to current head position as the next request
            for (int i = 0; i < IO_queue.size(); i++) {
                if (abs(IO_queue[i]->trackNumber - currPos) < nearest_dis) {
                    nearest_dis = abs(IO_queue[i]->trackNumber - currPos);
                    next_request = IO_queue[i];
                    idx_to_erase = i;
                }
            }
            // Delete the IO request selected for implementation
            IO_queue.erase(IO_queue.begin() + idx_to_erase);
            return next_request;
        }
        // Enqueue
        void enqueue(IO_Operation* op) override {
            IO_queue.push_back(op);
        }
        // Whether the queue is empty
        bool isEmpty() override {
            if (IO_queue.empty()) {
                return true;
            } else {
                return false;
            }
        }
};

// LOOK
class LOOK: public Scheduler {
    private:
        vector<IO_Operation*> IO_queue;  // Use vector for easier traversal
    public:
        IO_Operation* get_next_request() override {
            IO_Operation* next_request = nullptr;
            int idx_to_erase = -1;
            if (direction == -1) {  
                // Forwards iteration
                for (int i = 0; i < IO_queue.size(); i++) {
                    // -1 is Correct direction -> Find the biggest IO that is smaller than currPos from the sorted queue
                    if (currPos >= IO_queue[i]->trackNumber) {
                        // If there are at least 2 IO requests with the same track number, select the IO that arrives earlier (smaller operation_id)
                        if (next_request == nullptr || 
                            IO_queue[i]->trackNumber > next_request->trackNumber || 
                            (IO_queue[i]->trackNumber == next_request->trackNumber && IO_queue[i]->operation_id < next_request->operation_id)) {
                            next_request = IO_queue[i];
                            idx_to_erase = i;
                        }
                    } else {
                        break; // Since the queue is sorted by track number
                    }
                }
                // -1 is Wrong direction -> Return the smallest IO since it will be the nearest after changing the direction (the 1st IO request)
                if (idx_to_erase == -1) {
                    next_request = IO_queue[0];
                    idx_to_erase = 0;
                }
            } else if (direction == 1) {
                // Backwards iteration
                for (int i = IO_queue.size() - 1; i >= 0; i--) {
                    // 1 is correct direction -> Find the smallest IO that is bigger than currPos from the sorted queue
                    if (IO_queue[i]->trackNumber >= currPos) {
                        if (next_request == nullptr || 
                            IO_queue[i]->trackNumber < next_request->trackNumber || 
                            (IO_queue[i]->trackNumber == next_request->trackNumber && IO_queue[i]->operation_id < next_request->operation_id)) {
                            next_request = IO_queue[i];
                            idx_to_erase = i;
                        }
                    } else {
                        break; 
                    }
                }
                // 1 is wrong direction -> Return the biggest IO since it will be the nearest after changing the direction
                if (idx_to_erase == -1) {
                    next_request = IO_queue[IO_queue.size()-1];
                    idx_to_erase = IO_queue.size()-1;
                }
            } 
            IO_queue.erase(IO_queue.begin() + idx_to_erase);
            return next_request;
        }
        // Enqueue: Sort by track number when doing enqueue
        void enqueue(IO_Operation* op) override {
            for (int i = 0; i < IO_queue.size(); i++) {
                if (op->trackNumber < IO_queue[i]->trackNumber) {
                    IO_queue.insert(IO_queue.begin() + i, op);
                    return;
                }
            }
            IO_queue.push_back(op);  // If no suitalbe position is found, insert at the end (its track number is the biggest)
        }
        // Whether the queue is empty
        bool isEmpty() override {
            if (IO_queue.empty()) {
                return true;
            } else {
                return false;
            }
        }
};

// CLOOK
class CLOOK: public Scheduler {
    private:
        vector<IO_Operation*> IO_queue;  // Use vector for easier traversal
    public:
        IO_Operation* get_next_request() override {
            IO_Operation* next_request = nullptr;
            int idx_to_erase = -1;

            // Always move forward (no direction)
            for (int i = 0; i < IO_queue.size(); i++) {
                if (IO_queue[i]->trackNumber >= currPos) {
                    next_request = IO_queue[i];
                    idx_to_erase = i;
                    break;
                }
            }
            // Wrap around to the first request if end is reached without finding a suitable request
            if (idx_to_erase == -1) {
                next_request = IO_queue.front();
                idx_to_erase = 0;
            }
            // Erase the request from the queue
            IO_queue.erase(IO_queue.begin() + idx_to_erase);
            return next_request;
        }
        // Enqueue: Sort by track number when doing enqueue
        void enqueue(IO_Operation* op) override {
            for (int i = 0; i < IO_queue.size(); i++) {
                if (op->trackNumber < IO_queue[i]->trackNumber) {
                    IO_queue.insert(IO_queue.begin() + i, op);
                    return;
                }
            }
            IO_queue.push_back(op);  // If no suitalbe position is found, insert at the end (its track number is the biggest)
        }
        // Whether the queue is empty
        bool isEmpty() override {
            if (IO_queue.empty()) {
                return true;
            } else {
                return false;
            }
        }
};

// FLOOK
class FLOOK: public Scheduler { 
    private:
        std::vector<IO_Operation*> active_queue;
        std::vector<IO_Operation*> wait_queue;
    public:
        IO_Operation* get_next_request() override {
            // Ensure there are requests to process
            if (active_queue.empty() && wait_queue.empty()) {
                return nullptr;
            }
            // Swap the queues if needed
            if (active_queue.empty()) {
                active_queue.swap(wait_queue);
                wait_queue.clear();
            }

            IO_Operation* next_request = nullptr; 
            int idx_to_erase = -1;
            // Modify the LOOK scheduler slightly
            if (direction == -1) {  
                // Forwards iteration
                for (int i = 0; i < active_queue.size(); i++) {
                    // -1 is Correct direction -> Find the biggest IO that is smaller than currPos from the sorted queue
                    if (currPos >= active_queue[i]->trackNumber) {
                        // If there are at least 2 IO requests with the same track number, select the IO that arrives earlier (smaller operation_id)
                        if (next_request == nullptr || 
                            active_queue[i]->trackNumber > next_request->trackNumber || 
                            (active_queue[i]->trackNumber == next_request->trackNumber && active_queue[i]->operation_id < next_request->operation_id)) {
                            next_request = active_queue[i];
                            idx_to_erase = i;
                        }
                    } else {
                        break; // Since the queue is sorted by track number
                    }
                }
                // -1 is Wrong direction -> Return the smallest IO since it will be the nearest after changing the direction (the 1st IO request)
                if (idx_to_erase == -1 && !active_queue.empty()) {
                    next_request = active_queue[0];
                    idx_to_erase = 0;
                }
            } else if (direction == 1 ) {
                // Backwards iteration
                for (int i = active_queue.size() - 1; i >= 0; i--) {
                    // 1 is correct direction -> Find the smallest IO that is bigger than currPos from the sorted queue
                    if (active_queue[i]->trackNumber >= currPos) {
                        if (next_request == nullptr || 
                            active_queue[i]->trackNumber < next_request->trackNumber || 
                            (active_queue[i]->trackNumber == next_request->trackNumber && active_queue[i]->operation_id < next_request->operation_id)) {
                            next_request = active_queue[i];
                            idx_to_erase = i;
                        }
                    } else {
                        break; 
                    }
                }
                // 1 is wrong direction -> Return the biggest IO since it will be the nearest after changing the direction
                if (idx_to_erase == -1 && !active_queue.empty()) {
                    next_request = active_queue[active_queue.size()-1];
                    idx_to_erase = active_queue.size()-1;
                }
            } 
            active_queue.erase(active_queue.begin() + idx_to_erase);
            return next_request;
        }
        // Enqueue: Sort by track number when doing enqueue
        void enqueue(IO_Operation* op) override {
            for (int i = 0; i < wait_queue.size(); i++) {
                if (op->trackNumber < wait_queue[i]->trackNumber) {
                    wait_queue.insert(wait_queue.begin() + i, op);
                    return;
                }
            }
            wait_queue.push_back(op);  // If no suitalbe position is found, insert at the end (its track number is the biggest)
        }
        // Whether the queue is empty
        bool isEmpty() override {
            if (active_queue.empty() && wait_queue.empty()) {
                return true;
            } else {
                return false;
            }
        }
};

Scheduler* scheduler;

void insertIoInfo(IO_Operation* newIoInfo) {
    vector<IO_Operation*>::iterator position = lower_bound(complete_IOs.begin(), complete_IOs.end(), newIoInfo, 
        [](const IO_Operation* Io1, const IO_Operation* Io2) -> bool {
            return Io1->operation_id < Io2->operation_id;
        });
    // Insert the newProcessInfo at the correct position to maintain the sort order
    complete_IOs.insert(position, newIoInfo);
}

// Print: for each IO request create an info line
void ioInfo_printer(const vector<IO_Operation*>& complete_IOs) {
    for (vector<IO_Operation*>::const_iterator io = complete_IOs.begin(); io != complete_IOs.end(); io++) {
        printf("%5d: %5d %5d %5d\n", (*io)->operation_id, (*io)->arrivalTime, (*io)->startTime, (*io)->endTime);
    }
}

// Calculate summary metrics
double get_avg_turnaround(int ttl_turnaround) { return ttl_turnaround / double(IO_list.size()); }
double get_avg_waittime(int ttl_waittime) { return ttl_waittime / double(IO_list.size()); }
double get_io_utilization(int ttlIoBusyTime, int total_time) { return ttlIoBusyTime / double(total_time); }

// Print summary
void summary_printer() {
    printf("SUM: %d %d %.4lf %.2lf %.2lf %d\n", total_time, tot_movement, get_io_utilization(ttlIoBusyTime, total_time), get_avg_turnaround(ttl_turnaround), get_avg_waittime(ttl_waittime), max_waittime);
}


int main(int argc, char *argv[]) {
    int opt;
    char algo = '\0';  // Selected IO scheduler
    string options;  // Optional output options
    string inputFile;
    while ((opt = getopt(argc, argv, "s:vqf")) != -1) {
        switch (opt) {
            case 's':
                algo = optarg[0];
                break;
            case 'v':
                V_flag = true;
                break;
            case 'q':
                Q_flag = true;
                break;
            case 'f':
                F_flag = true;
                break;
            default:
                return 1;
        }
    }
    // inputFile after the processing options
    if (optind < argc) {
        inputFile = argv[optind];  // The first non-option argument is the input file
    } else {
        cout << "Input file is required." << endl;
        return 1;
    }
    Scheduler* scheduler = new FIFO();  // Default scheduer: FCFS
    if (algo == 'N') { scheduler = new FIFO(); }
    if (algo == 'S') { scheduler = new SSTF(); }
    if (algo == 'L') { scheduler = new LOOK(); }
    if (algo == 'C') { scheduler = new CLOOK(); }
    if (algo == 'F') { scheduler = new FLOOK(); }

    // Read input file
    ifstream inputFileStream(inputFile);
    if (!inputFileStream.is_open()) {
        cout << "Failed to open input file." << endl;
        return 1;
    }
    string line;
    // Skip initial comment lines: continue looping until finding a line that is not empty and does not start with '#'
    while (getline(inputFileStream, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        istringstream iss(line);
        int arriveTime, trackNum;
        if (iss >> arriveTime >> trackNum) {
            IO_Operation* currOperation = new IO_Operation(operationCnt, arriveTime, trackNum);
            IO_list.push_back(currOperation);
        }
        operationCnt++;
    }
    inputFileStream.close();

    // Simulation
    int index = 0;  // Used for traversal of the vector
    while (true) {
        // if new I/O's arrived at the system at this current time, add request to the IO-queue
        while (index < IO_list.size() && IO_list[index]->arrivalTime == currentTime) {
            scheduler->enqueue(IO_list[index]);
            if (V_flag) { cout << currentTime << ": " << IO_list[index]->operation_id << " add " << IO_list[index]->trackNumber << endl; }
            index++;
        }
        // if an IO is active, compute relevant info and store in the IO request for final summary
        if (isActiveIO) {
            // if current active IO is about to complete
            if (activeIO->trackNumber == currPos) {
                activeIO->endTime = currentTime;
                total_time = activeIO->endTime;  // Update total_time
                if (V_flag) { cout << currentTime << ": " << activeIO->operation_id << " finish " << currentTime - activeIO->arrivalTime << " currPos: " << currPos << endl; }
                isActiveIO = false;
                insertIoInfo(activeIO);
                ttlIoBusyTime += activeIO->endTime - activeIO->startTime;  // Update ttlIoBusyTime
                ttl_turnaround += activeIO->endTime - activeIO->arrivalTime;  // Update ttl_turnaround
                ttl_waittime += activeIO->startTime - activeIO->arrivalTime;  // Update ttl_waittime
                // Update max_waittime
                int curr_waittime = activeIO->startTime - activeIO->arrivalTime;
                if (curr_waittime > max_waittime) {
                    max_waittime = curr_waittime;
                }
                continue;
            // if current active IO will not complete
            } else {  
                isActiveIO = true;
                currPos += direction;  // Move the head ahead one
                tot_movement++;  // Update tot_movement
            }
        // if no IO request active now
        } else { 
            if (!scheduler->isEmpty() && !isActiveIO) {
                // push the next IO operation that can be executed to IO queue and update currentTime
                activeIO = scheduler->get_next_request();
                // update direction
                if (activeIO->trackNumber - currPos > 0) {
                    direction = 1;
                } else if (activeIO->trackNumber - currPos < 0) {
                    direction = -1;
                } 
                isActiveIO = true;
                if (V_flag) { cout << currentTime << ": " << activeIO->operation_id << " issue " << activeIO->trackNumber << " " << currPos << endl; }
                activeIO->startTime = currentTime;  // Set the start time
                // If the next IO request's track number == currPos, it will finish immediately -> no need to +1 to currentTime (continue; directly) or do any movement
                if (activeIO->trackNumber == currPos) { continue; }
                // adjust currPos accordingly by the direction for the newly executed IO operation
                if (activeIO->trackNumber != currPos) {
                    currPos += direction;
                    tot_movement++;  // Update tot_movement
                }
            }
            // all IO from input file processed
            if (index >= IO_list.size() && scheduler->isEmpty() && !isActiveIO) {
                break;
            }
        }  
        currentTime++;  // Time always progresses regardless of whether an I/O is active
    }
    // Print IO info
    ioInfo_printer(complete_IOs);
    summary_printer();

    return 0;
}