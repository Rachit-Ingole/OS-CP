// ============================================================
//  Multiprogramming OS Simulator — Phase 2
//  Features:
//    1. Paging (logical -> physical address translation)
//    2. Interrupt Handling: SI, PI, TI
//    3. Error codes 0-9
//    4. Time Limit (TTL) and Line Limit (TLL) enforcement
// ============================================================

#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <ctime>
#include <cstring>

using namespace std;

// ─────────────────────────────────────────────
//  Memory & Registers
// ─────────────────────────────────────────────
// 300 frames of 4 bytes each (30 physical pages x 10 words)
char M[300][4];

char IR[4];   // Instruction Register
char R[4];    // General-Purpose Register
int  IC;      // Instruction Counter (logical)
int  C;       // Compare/Toggle flag (0 or 1)

// ─────────────────────────────────────────────
//  Interrupt Registers
// ─────────────────────────────────────────────
int SI;  // Service Interrupt   (1=GD, 2=PD, 3=H)
int PI;  // Program Interrupt   (1=BadOpcode, 2=BadOperand, 3=PageFault)
int TI;  // Timer Interrupt     (0=ok, 2=time expired)

// ─────────────────────────────────────────────
//  Process Control Block
// ─────────────────────────────────────────────
struct PCB {
    char JID[5]; // Job ID (4 chars + null)
    int  TTL;    // Total Time Limit
    int  TLL;    // Total Line Limit
};
PCB pcb;

int TTC;  // Total Time Counter
int LLC;  // Line (output) Counter

// ─────────────────────────────────────────────
//  Paging
// ─────────────────────────────────────────────
int PTR;                  // Page Table Register (base row in M for page table)
int pageTableCount;       // How many page table entries have been filled

// Logical page -> physical frame mapping used during address translation
// We store it in the page table inside M[PTR + page_no]
// M[PTR+i][0] = '1' means valid; [2][3] hold the frame number digits

// Track which physical frames have been allocated
bool frameUsed[30];

// pageCount tracks how many instruction pages have been loaded
int pageCount;

// ─────────────────────────────────────────────
//  I/O helpers
// ─────────────────────────────────────────────
fstream infile;
fstream outfile;
bool    firstOutput;    // suppress leading newline in output

int  terminationMsg;   // 0-9 error code for end-of-job report
bool terminated;       // flag: job has ended
bool hitEndOfData;     // GD saw $END marker (out of data)

// ─────────────────────────────────────────────
//  Forward declarations
// ─────────────────────────────────────────────
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

// ============================================================
//  INIT — reset all state for a new job
// ============================================================
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
    memset(pcb.JID, 0, sizeof(pcb.JID));

    PTR            = 0;
    pageTableCount = 0;
    pageCount      = 0;

    memset(frameUsed, false, sizeof(frameUsed));

    firstOutput    = true;
    terminationMsg = 0;
    terminated     = false;
    hitEndOfData   = false;
}

// ============================================================
//  allocateFrame — pick an unused physical frame (0-29)
// ============================================================
int allocateFrame() {
    // Simple sequential scan; could be randomised like the reference impl
    for (int f = 0; f < 30; f++) {
        if (!frameUsed[f]) {
            frameUsed[f] = true;
            return f;
        }
    }
    // Should not happen in a well-formed job
    return -1;
}

// ============================================================
//  addressMap — translate logical IC to physical address
//  The page table lives in M[PTR .. PTR+9].
//  Each entry M[PTR+p] stores frame number in [2][3].
// ============================================================
int addressMap(int logicalIC) {
    int page   = logicalIC / 10;   // which page (0-indexed)
    int offset = logicalIC % 10;   // word within page

    // Read frame from page table
    int frame = (M[PTR + page][2] - '0') * 10 + (M[PTR + page][3] - '0');
    return frame * 10 + offset;
}

// ============================================================
//  operandMap — translate a logical operand address to physical
//  If the page is not yet mapped:
//    - For GD / SR (write ops): allocate a new frame (valid page fault)
//    - For anything else:       PI=3, invalid page fault
// ============================================================
int operandMap(int logicalAddr, bool writeOp) {
    // BT uses address as a new IC, not a memory address
    if (IR[0] == 'B' && IR[1] == 'T')
        return logicalAddr;

    // H has no operand
    if (IR[0] == 'H')
        return -1;

    // Validate operand digits
    if (IR[2] < '0' || IR[2] > '9' || IR[3] < '0' || IR[3] > '9') {
        PI = 2; // operand error
        return -1;
    }

    int page   = logicalAddr / 10;
    int offset = logicalAddr % 10;

    // Check page table entry
    if (M[PTR + page][0] == '1') {
        // Valid entry
        int frame = (M[PTR + page][2] - '0') * 10 + (M[PTR + page][3] - '0');
        return frame * 10 + offset;
    }

    // Page not mapped — page fault
    if (writeOp) {
        // Valid page fault: allocate frame
        int frame = allocateFrame();
        if (frame < 0) { PI = 3; return -1; }

        M[PTR + page][0] = '1';
        M[PTR + page][2] = (char)('0' + frame / 10);
        M[PTR + page][3] = (char)('0' + frame % 10);
        pageTableCount++;

        return frame * 10 + offset;
    } else {
        // Invalid page fault
        PI = 3;
        return -1;
    }
}

// ============================================================
//  printEndOfJob — write job summary to output file
// ============================================================
void printEndOfJob() {
    outfile << "\n--- Job ID: ";
    for (int i = 0; i < 4; i++) outfile << pcb.JID[i];
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
    outfile << "TTC = " << TTC << " / " << pcb.TTL << "\n";
    outfile << "LLC = " << LLC << " / " << pcb.TLL << "\n";
    outfile << "\n\n";
}

// ============================================================
//  TERMINATE — set terminationMsg and stop execution
// ============================================================
void TERMINATE(int em) {
    terminationMsg = em;
    terminated     = true;
}

// ============================================================
//  GD — Get Data from input into memory
//  Reads the next data line sequentially from infile.
//  (No seekg needed — infile is positioned after $DTA by LOAD)
// ============================================================
void GD() {
    // Map operand (write op = true since we're writing data into memory)
    int logAddr  = (IR[2] - '0') * 10 + (IR[3] - '0');
    int physAddr = operandMap(logAddr, true);
    if (PI != 0) return; // page fault or operand error already set

    string line;
    if (!getline(infile, line)) {
        TERMINATE(1); // out of data (EOF)
        return;
    }
    // Strip Windows-style carriage return
    if (!line.empty() && line.back() == '\r') line.pop_back();

    // Check if we hit $END — means no more data for this job
    if (line.size() >= 4 && line.substr(0, 4) == "$END") {
        hitEndOfData = true; // tell LOAD skip-loop not to consume $END again
        TERMINATE(1);
        return;
    }

    // Write up to 40 chars into memory (10 words x 4 bytes)
    // physAddr is the starting word row in M
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

// ============================================================
//  PD — Print Data from memory to output file
// ============================================================
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

    // Print 10 words (40 chars) trimmed
    string output = "";
    for (int i = physAddr; i < physAddr + 10; i++) {
        for (int j = 0; j < 4; j++) {
            output += M[i][j];
        }
    }
    // Trim trailing spaces
    size_t end = output.find_last_not_of(" \t");
    if (end != string::npos)
        output = output.substr(0, end + 1);
    else
        output = "";

    outfile << output;
    SI = 0;
}

// ============================================================
//  H — Halt (normal termination)
// ============================================================
void H() {
    TERMINATE(0);
}

// ============================================================
//  LR — Load Register: R = M[operand]
// ============================================================
void LR() {
    int logAddr  = (IR[2] - '0') * 10 + (IR[3] - '0');
    int physAddr = operandMap(logAddr, false);
    if (PI != 0) return;
    for (int i = 0; i < 4; i++) R[i] = M[physAddr][i];
}

// ============================================================
//  SR — Store Register: M[operand] = R
// ============================================================
void SR() {
    int logAddr  = (IR[2] - '0') * 10 + (IR[3] - '0');
    int physAddr = operandMap(logAddr, true);
    if (PI != 0) return;
    for (int i = 0; i < 4; i++) M[physAddr][i] = R[i];
}

// ============================================================
//  CR — Compare Register: C = (R == M[operand])
// ============================================================
void CR() {
    int logAddr  = (IR[2] - '0') * 10 + (IR[3] - '0');
    int physAddr = operandMap(logAddr, false);
    if (PI != 0) return;
    C = 1;
    for (int i = 0; i < 4; i++) {
        if (R[i] != M[physAddr][i]) { C = 0; break; }
    }
}

// ============================================================
//  BT — Branch if Toggle: if C==1 then IC = operand
// ============================================================
void BT() {
    if (C == 1) {
        IC = (IR[2] - '0') * 10 + (IR[3] - '0');
        C  = 0;
    }
}

// ============================================================
//  simulateTick — increment TTC, set TI if limit exceeded
// ============================================================
void simulateTick() {
    TTC++;
    if (TTC > pcb.TTL) {
        TI = 2;
    }
}

// ============================================================
//  MOS — Master Operating System interrupt handler
//  Called whenever SI, PI, or TI is non-zero.
//
//  Interrupt priority table (from spec):
//   TI=0, SI=1          → GD
//   TI=0, SI=2          → PD
//   TI=0, SI=3          → H (terminate EM=0)
//   TI=0, PI=1          → terminate EM=4
//   TI=0, PI=2          → terminate EM=5
//   TI=0, PI=3 (valid)  → allocate frame, retry
//   TI=0, PI=3 (invalid)→ terminate EM=6
//   TI=2, PI=0, SI=2    → PD then terminate EM=3
//   TI=2, PI=0, SI=3    → H then return (no error)
//   TI=2, PI=0, other   → terminate EM=3
//   TI=2, PI=1          → terminate EM=7
//   TI=2, PI=2          → terminate EM=8
//   TI=2, PI=3          → terminate EM=9
// ============================================================
void MOS() {
    if (TI == 0) {
        if (PI == 0) {
            // Normal service interrupt
            if (SI == 1) { GD(); }
            else if (SI == 2) { PD(); }
            else if (SI == 3) { H(); }
        }
        else if (PI == 1) { TERMINATE(4); }
        else if (PI == 2) { TERMINATE(5); }
        else if (PI == 3) {
            // Page fault — handle inside operandMap already
            // If we reach here the page fault was invalid
            TERMINATE(6);
        }
    }
    else if (TI == 2) {
        if (PI == 0) {
            if (SI == 2) {
                // Allow the current PD to complete then terminate
                PD();
            } else if (SI == 3) {
                H(); // halts cleanly, no time-limit error message needed
                return;
            }
            TERMINATE(3);
        }
        else if (PI == 1) { TERMINATE(7); }
        else if (PI == 2) { TERMINATE(8); }
        else if (PI == 3) { TERMINATE(9); }
    }

    // Reset interrupt registers
    SI = 0;
    PI = 0;
}

// ============================================================
//  EXECUTE_USER_PROGRAM — main fetch-decode-execute loop
// ============================================================
void EXECUTE_USER_PROGRAM() {
    while (!terminated) {
        // ── FETCH ──────────────────────────────────────────
        int physIC = addressMap(IC);
        for (int i = 0; i < 4; i++) IR[i] = M[physIC][i];
        IC++;

        // ── TICK (count instruction, check time limit) ─────
        simulateTick();

        // ── DECODE operand address ─────────────────────────
        // (actual memory access happens inside each instruction)

        // ── CHECK OPCODE ───────────────────────────────────
        bool validOp = (
            (IR[0]=='G' && IR[1]=='D') ||
            (IR[0]=='P' && IR[1]=='D') ||
            (IR[0]=='H'               ) ||
            (IR[0]=='L' && IR[1]=='R') ||
            (IR[0]=='S' && IR[1]=='R') ||
            (IR[0]=='C' && IR[1]=='R') ||
            (IR[0]=='B' && IR[1]=='T')
        );

        if (!validOp) {
            PI = 1; // opcode error
        }

        // ── HANDLE INTERRUPTS via MOS ──────────────────────
        if (TI != 0 || PI != 0) {
            MOS();
            break;
        }

        // ── EXECUTE ────────────────────────────────────────
        if      (IR[0]=='G' && IR[1]=='D') { SI=1; GD(); if (terminated) break; }
        else if (IR[0]=='P' && IR[1]=='D') { SI=2; PD(); if (terminated) break; }
        else if (IR[0]=='H'               ) { SI=3; H();  break; }
        else if (IR[0]=='L' && IR[1]=='R') { LR(); }
        else if (IR[0]=='S' && IR[1]=='R') { SR(); }
        else if (IR[0]=='C' && IR[1]=='R') { CR(); }
        else if (IR[0]=='B' && IR[1]=='T') { BT(); }

        // After execution, check if PI was set by operand map
        if (PI != 0 || TI != 0) {
            MOS();
            break;
        }
    }
}

// ============================================================
//  START_EXECUTION
// ============================================================
void START_EXECUTION() {
    IC = 0;
    EXECUTE_USER_PROGRAM();
}

// ============================================================
//  LOAD — read input file, load jobs one by one
// ============================================================
void LOAD(const string &inPath, const string &outPath) {
    infile.open(inPath,  ios::in);
    outfile.open(outPath, ios::out);

    if (!infile.is_open()) {
        cerr << "ERROR: Cannot open input file: " << inPath << "\n";
        return;
    }

    string line;
    while (getline(infile, line)) {
        // Strip trailing CR (Windows line endings)
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.size() < 4) continue;

        // ── $AMJ — start of a new job ──────────────────────
        if (line.substr(0, 4) == "$AMJ") {
            INIT();

            // Parse JID (chars 4-7)
            for (int i = 0; i < 4 && (4 + i) < (int)line.size(); i++)
                pcb.JID[i] = line[4 + i];
            pcb.JID[4] = '\0';

            // Parse TTL (chars 8-11) and TLL (chars 12-15)
            pcb.TTL = 0; pcb.TLL = 0;
            for (int i = 8; i < 12 && i < (int)line.size(); i++)
                pcb.TTL = pcb.TTL * 10 + (line[i] - '0');
            for (int i = 12; i < 16 && i < (int)line.size(); i++)
                pcb.TLL = pcb.TLL * 10 + (line[i] - '0');

            // Allocate page table frame
            int ptFrame = allocateFrame();
            PTR = ptFrame * 10;

            // Mark all page table entries as invalid
            for (int i = PTR; i < PTR + 10; i++) {
                M[i][0] = '0';
                M[i][1] = ' ';
                M[i][2] = '*';
                M[i][3] = '*';
            }

            // ── Load program cards until $DTA ───────────────
            while (getline(infile, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.size() >= 4 && line.substr(0, 4) == "$DTA") break;

                // Allocate a frame for this instruction page
                int frame = allocateFrame();
                int physStart = frame * 10;

                // Record mapping in page table
                M[PTR + pageTableCount][0] = '1';
                M[PTR + pageTableCount][2] = (char)('0' + frame / 10);
                M[PTR + pageTableCount][3] = (char)('0' + frame % 10);
                pageTableCount++;

                // Load up to 40 chars into 10 words of this frame
                char buf[40];
                memset(buf, ' ', 40);
                for (size_t i = 0; i < line.size() && i < 40; i++)
                    buf[i] = line[i];

                for (int i = 0; i < 10; i++)
                    for (int j = 0; j < 4; j++)
                        M[physStart + i][j] = buf[i * 4 + j];
            }

            // infile is now positioned at the first data line — GD reads sequentially

            // ── Execute the job ─────────────────────────────
            START_EXECUTION();

            // ── Skip remaining data/leftover lines until $END ──
            // If GD already consumed $END (hitEndOfData), skip the loop
            if (!hitEndOfData) {
                while (getline(infile, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (line.size() >= 4 && line.substr(0, 4) == "$END") break;
                }
            }

            // ── Print end-of-job summary ────────────────────
            printEndOfJob();
        }
        // (Any stray $DTA / $END at top-level are ignored here)
    }

    infile.close();
    outfile.close();
}

// ============================================================
//  main
// ============================================================
int main() {
    cout << "============================================\n";
    cout << "  MOS Phase 2 Simulator\n";
    cout << "  Features: Paging | Interrupts | Limits\n";
    cout << "============================================\n\n";

    LOAD("input_phase2.txt", "output_phase2.txt");

    cout << "\nPhase 2 simulation complete.\n";
    cout << "Output written to: output_phase2.txt\n";
    return 0;
}
