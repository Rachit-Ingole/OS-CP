#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <ctime>

using namespace std;

// 300 frames of 4 bytes each (30 physical pages x 10 words)
char M[300][4];

char IR[4];   // Instruction Register
char R[4];    // General-Purpose Register
int  IC;      // Instruction Counter (logical)
int  C;       // Compare/Toggle flag (0 or 1)

int SI;  // Service Interrupt   (1=GD, 2=PD, 3=H)
int PI;  // Program Interrupt   (1=BadOpcode, 2=BadOperand, 3=PageFault)
int TI;  // Timer Interrupt     (0=ok, 2=time expired)

struct PCB {
    string JID;  // Job ID (4 chars)
    int  TTL;    // Total Time Limit
    int  TLL;    // Total Line Limit
};
PCB pcb;

int TTC;  // Total Time Counter
int LLC;  // Line (output) Counter

int PTR;// Page Table Register (base row in M for page table)
int pageTableCount;// How many page table entries have been filled

bool frameUsed[30];

int pageCount;

fstream infile;
fstream outfile;
bool    firstOutput;    // suppress leading newline in output

int  terminationMsg;   // 0-9 error code for end-of-job report
bool terminated;       // flag: job has ended
bool hitEndOfData;     // GD saw $END marker (out of data)

void INIT();
int  allocateFrame();
int  addressMap(int logicalIC);
int  operandMap(int logicalAddr, bool writeOp);
void MOS();
void GD();
void PD();
void H();
void LR();
void SR();
void CR();
void BT();
void simulateTick();
void EXECUTE_USER_PROGRAM();
void START_EXECUTION();
void LOAD(const string &inPath, const string &outPath);
void printEndOfJob();

void INIT() {
    for (int i = 0; i < 300; i++)
        for (int j = 0; j < 4; j++)
            M[i][j] = ' ';

    for (int i = 0; i < 4; i++) {
        IR[i] = ' ';
        R[i]  = ' ';
    }

    IC  = 0;
    C   = 0;
    SI  = 0;
    PI  = 0;
    TI  = 0;
    TTC = 0;
    LLC = 0;

    pcb.TTL = 0;
    pcb.TLL = 0;
    pcb.JID.clear();

    PTR            = 0;
    pageTableCount = 0;
    pageCount      = 0;

    for (int i = 0; i < 30; i++) {
        frameUsed[i] = false;
    }

    firstOutput    = true;
    terminationMsg = 0;
    terminated     = false;
    hitEndOfData   = false;
}

int allocateFrame() {
    for (int f = 0; f < 30; f++) {
        if (!frameUsed[f]) {
            frameUsed[f] = true;
            return f;
        }
    }
    return -1;
}

int addressMap(int logicalIC) {
    int page   = logicalIC / 10;   // which page (0-indexed)
    int offset = logicalIC % 10;   // word within page

    // Read frame from page table
    int frame = (M[PTR + page][2] - '0') * 10 + (M[PTR + page][3] - '0');
    return frame * 10 + offset;
}

//For GD / SR (write ops): allocate a new frame (valid page fault)
//For anything else:       PI=3, invalid page fault
int operandMap(int logicalAddr, bool writeOp) {
    if (IR[0] == 'B' && IR[1] == 'T')
        return logicalAddr;

    if (IR[0] == 'H')
        return -1;

    if (IR[2] < '0' || IR[2] > '9' || IR[3] < '0' || IR[3] > '9') {
        PI = 2; // operand error
        return -1;
    }

    int page   = logicalAddr / 10;
    int offset = logicalAddr % 10;

    if (M[PTR + page][0] == '1') {
        int frame = (M[PTR + page][2] - '0') * 10 + (M[PTR + page][3] - '0');
        return frame * 10 + offset;
    }

    if (writeOp) {
        // Valid page fault
        int frame = allocateFrame();
        if (frame < 0) { PI = 3; return -1; }

        M[PTR + page][0] = '1';
        M[PTR + page][2] = (char)('0' + frame / 10);
        M[PTR + page][3] = (char)('0' + frame % 10);
        pageTableCount++;

        return frame * 10 + offset;
    } else {
        PI = 3; //page fault
        return -1;
    }
}

void printEndOfJob() {
    outfile << "\n--- Job ID: ";
    outfile << pcb.JID;
    outfile << " ---\n";

    switch (terminationMsg) {
        case 0: outfile << "Status : Normal termination (no error)\n"; break;
        case 1: outfile << "Status : Out of data\n"; break;
        case 2: outfile << "Status : Line limit exceeded\n"; break;
        case 3: outfile << "Status : Time limit exceeded\n"; break;
        case 4: outfile << "Status : Operation code error\n"; break;
        case 5: outfile << "Status : Operand error\n"; break;
        case 6: outfile << "Status : Invalid page fault\n"; break;
        case 7: outfile << "Status : Time limit exceeded AND operation code error\n"; break;
        case 8: outfile << "Status : Time limit exceeded AND operand error\n"; break;
        case 9: outfile << "Status : Time limit exceeded AND invalid page fault\n"; break;
        default: outfile << "Status : Unknown error\n"; break;
    }

    outfile << "IC  = " << IC  << "\n";
    outfile << "IR  = " << IR[0] << IR[1] << IR[2] << IR[3] << "\n";
    outfile << "TTC = " << TTC << " / " << pcb.TTL << "\n";
    outfile << "LLC = " << LLC << " / " << pcb.TLL << "\n";
    outfile << "\n\n";
}

void TERMINATE(int em) {
    terminationMsg = em;
    terminated= true;
}


void GD() {
    int logAddr  = (IR[2] - '0') * 10 + (IR[3] - '0');
    int physAddr = operandMap(logAddr, true);
    if (PI != 0) return; // page fault or operand error

    string line;
    if (!getline(infile, line)) {
        TERMINATE(1); // out of data
        return;
    }

    if (!line.empty() && line.back() == '\r') line.pop_back();

    if (line.size() >= 4 && line.substr(0, 4) == "$END") {
        hitEndOfData = true;
        TERMINATE(1);
        return;
    }

    int startRow = physAddr;
    int col = 0;
    for (size_t i = 0; i < line.size() && i < 40; i++) {
        M[startRow + col / 4][col % 4] = line[i];
        col++;
    }
    // Pad remainder with spaces
    for (; col < 40; col++) {
        M[startRow + col / 4][col % 4] = ' ';
    }

    SI = 0;
}

void PD() {
    LLC++;
    if (LLC > pcb.TLL) {
        TERMINATE(2); // line limit exceeded
        return;
    }

    int logAddr  = (IR[2] - '0') * 10 + (IR[3] - '0');
    int physAddr = operandMap(logAddr, false);
    if (PI != 0) return;

    if (!firstOutput) outfile << "\n";
    firstOutput = false;

    string output = "";
    for (int i = physAddr; i < physAddr + 10; i++) {
        for (int j = 0; j < 4; j++) {
            output += M[i][j];
        }
    }

    size_t end = output.find_last_not_of(" \t");
    if (end != string::npos)
        output = output.substr(0, end + 1);
    else
        output = "";

    outfile << output;
    SI = 0;
}

void H() {
    TERMINATE(0);
}

void LR() {
    int logAddr  = (IR[2] - '0') * 10 + (IR[3] - '0');
    int physAddr = operandMap(logAddr, false);
    if (PI != 0) return;
    for (int i = 0; i < 4; i++) R[i] = M[physAddr][i];
}

void SR() {
    int logAddr  = (IR[2] - '0') * 10 + (IR[3] - '0');
    int physAddr = operandMap(logAddr, true);
    if (PI != 0) return;
    for (int i = 0; i < 4; i++) M[physAddr][i] = R[i];
}

void CR() {
    int logAddr  = (IR[2] - '0') * 10 + (IR[3] - '0');
    int physAddr = operandMap(logAddr, false);
    if (PI != 0) return;
    C = 1;
    for (int i = 0; i < 4; i++) {
        if (R[i] != M[physAddr][i]) { C = 0; break; }
    }
}

void BT() {
    if (C == 1) {
        IC = (IR[2] - '0') * 10 + (IR[3] - '0');
        C  = 0;
    }
}

void simulateTick() {
    TTC++;
    if (TTC > pcb.TTL) {
        TI = 2;
    }
}

// ============================================================
//  MOS - Master Operating System interrupt handler
//  Called whenever SI, PI, or TI is non-zero.
//
//  Interrupt priority table (from spec):
//   TI=0, SI=1          -> GD
//   TI=0, SI=2          -> PD
//   TI=0, SI=3          -> H (terminate EM=0)
//   TI=0, PI=1          -> terminate EM=4
//   TI=0, PI=2          -> terminate EM=5
//   TI=0, PI=3 (valid)  -> allocate frame, retry
//   TI=0, PI=3 (invalid)-> terminate EM=6
//   TI=2, PI=0, SI=2    -> PD then terminate EM=3
//   TI=2, PI=0, SI=3    -> H then return (no error)
//   TI=2, PI=0, other   -> terminate EM=3
//   TI=2, PI=1          -> terminate EM=7
//   TI=2, PI=2          -> terminate EM=8
//   TI=2, PI=3          -> terminate EM=9
// ============================================================
void MOS() {
    if (TI == 0) {
        if (PI == 0) {
            if (SI == 1) { GD(); }
            else if (SI == 2) { PD(); }
            else if (SI == 3) { H(); }
        }
        else if (PI == 1) { TERMINATE(4); }
        else if (PI == 2) { TERMINATE(5); }
        else if (PI == 3) { TERMINATE(6); }
    }
    else if (TI == 2) {
        if (PI == 0) {
            if (SI == 2) {
                PD();
            } else if (SI == 3) {
                H();
                return;
            }
            TERMINATE(3);
        }
        else if (PI == 1) { TERMINATE(7); }
        else if (PI == 2) { TERMINATE(8); }
        else if (PI == 3) { TERMINATE(9); }
    }

    SI = 0;
    PI = 0;
}

void EXECUTE_USER_PROGRAM() {
    while (!terminated) {
        int physIC = addressMap(IC);
        for (int i = 0; i < 4; i++) IR[i] = M[physIC][i];
        IC++;

        simulateTick();

        bool validOp = (
            (IR[0]=='G' && IR[1]=='D') ||
            (IR[0]=='P' && IR[1]=='D') ||
            (IR[0]=='H') ||
            (IR[0]=='L' && IR[1]=='R') ||
            (IR[0]=='S' && IR[1]=='R') ||
            (IR[0]=='C' && IR[1]=='R') ||
            (IR[0]=='B' && IR[1]=='T')
        );

        if (!validOp) {
            PI = 1;
        }
        else if (IR[0] != 'H' &&
                 (IR[2] < '0' || IR[2] > '9' || IR[3] < '0' || IR[3] > '9')) {
            PI = 2;
        }

        if (TI != 0 || PI != 0) {
            MOS();
            break;
        }

        if(IR[0]=='G' && IR[1]=='D') { SI=1; GD(); if (terminated) break; }
        else if(IR[0]=='P' && IR[1]=='D') { SI=2; PD(); if (terminated) break; }
        else if(IR[0]=='H') { SI=3; H();  break; }
        else if(IR[0]=='L' && IR[1]=='R') { LR(); }
        else if(IR[0]=='S' && IR[1]=='R') { SR(); }
        else if(IR[0]=='C' && IR[1]=='R') { CR(); }
        else if(IR[0]=='B' && IR[1]=='T') { BT(); }

        if (TI != 0 || PI != 0) {
            MOS();
            break;
        }
    }
}

void START_EXECUTION() {
    IC = 0;
    EXECUTE_USER_PROGRAM();
}

void LOAD(const string &inPath, const string &outPath) {
    infile.open(inPath,  ios::in);
    outfile.open(outPath, ios::out);

    string line;
    while (getline(infile, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() < 4) continue;

        if (line.substr(0, 4) == "$AMJ") {
            INIT();

            pcb.JID = line.substr(4, 4);

            pcb.TTL = 0; pcb.TLL = 0;
            for (int i = 8; i < 12; i++)
                pcb.TTL = pcb.TTL * 10 + (line[i] - '0');
            for (int i = 12; i < 16; i++)
                pcb.TLL = pcb.TLL * 10 + (line[i] - '0');

            int ptFrame = allocateFrame();
            PTR = ptFrame * 10;

            for (int i = PTR; i < PTR + 10; i++) {
                M[i][0] = '0';
                M[i][1] = ' ';
                M[i][2] = '*';
                M[i][3] = '*';
            }

            while (getline(infile, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.size() >= 4 && line.substr(0, 4) == "$DTA") break;

                int frame = allocateFrame();
                int physStart = frame * 10;

                M[PTR + pageTableCount][0] = '1';
                M[PTR + pageTableCount][2] = (char)('0' + frame / 10);
                M[PTR + pageTableCount][3] = (char)('0' + frame % 10);
                pageTableCount++;

                char buf[40];
                for (int i = 0; i < 40; i++) {
                    buf[i] = ' ';
                }
                for (size_t i = 0; i < line.size() && i < 40; i++)
                    buf[i] = line[i];

                for (int i = 0; i < 10; i++)
                    for (int j = 0; j < 4; j++)
                        M[physStart + i][j] = buf[i * 4 + j];
            }

            START_EXECUTION();

            // -- Skip remaining data/leftover lines until $END --
            // If GD already consumed $END (hitEndOfData), skip the loop
            if (!hitEndOfData) {
                while (getline(infile, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (line.size() >= 4 && line.substr(0, 4) == "$END") break;
                }
            }

            printEndOfJob();
        }
    }

    infile.close();
    outfile.close();
}

int main() {
    LOAD("input_phase2.txt", "output_phase2.txt");

    cout << "\nPhase 2 simulation complete.\n";
    cout << "Output written to: output_phase2.txt\n";
    return 0;
}
