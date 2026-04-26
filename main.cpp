#include <iostream>
#include <fstream>
#include <string>

using namespace std;

char M[100][4];
char IR[4];
char R[4];
int IC;
bool C;
int SI;

char buffer[40];

fstream infile;
fstream outfile;

void INIT() {
    for (int i = 0; i < 100; i++) {
        for (int j = 0; j < 4; j++) {
            M[i][j] = ' ';
        }
    }
    for (int i = 0; i < 4; i++) {
        IR[i] = ' ';
        R[i] = ' ';
    }
    IC = 0;
    C = false;
    SI = 0;
}

void READ() {
    for(int i = 0; i < 40; i++) buffer[i] = ' ';
    string line;
    if(getline(infile, line)) {
        if(line.substr(0, 4) == "$END") {
            return;
        }
        int mem_start = (IR[2] - '0') * 10 + (IR[3] - '0');
        int b = 0;
        for(size_t i = 0; i < line.length() && i < 40; i++) {
            buffer[i] = line[i];
        }
        for(int i = 0; i < 10; i++) {
            for(int j = 0; j < 4; j++) {
                M[mem_start + i][j] = buffer[b++];
            }
        }
    }
}

void WRITE() {
    for(int i = 0; i < 40; i++) buffer[i] = ' ';
    int mem_start = (IR[2] - '0') * 10 + (IR[3] - '0');
    int b = 0;
    for(int i = 0; i < 10; i++) {
        for(int j = 0; j < 4; j++) {
            buffer[b++] = M[mem_start + i][j];
        }
    }
    string output = "";
    for(int i = 0; i < 40; i++) {
        output += buffer[i];
    }
    
    size_t endpos = output.find_last_not_of(" \t");
    if(string::npos != endpos) {
        output = output.substr(0, endpos + 1);
    } else {
        output = "";
    }
    outfile << output << "\n";
}

void TERMINATE() {
    outfile << "\n\n";
}

void MOS() {
    switch(SI) {
        case 1:
            READ();
            break;
        case 2:
            WRITE();
            break;
        case 3:
            TERMINATE();
            break;
    }
    SI = 0;
}

void EXECUTE_USER_PROGRAM() {
    while (true) {
        for (int i = 0; i < 4; i++) {
            IR[i] = M[IC][i];
        }
        IC++;

        if (IR[0] == 'L' && IR[1] == 'R') {
            int operand = (IR[2] - '0') * 10 + (IR[3] - '0');
            for(int i = 0; i < 4; i++) R[i] = M[operand][i];
        }
        else if (IR[0] == 'S' && IR[1] == 'R') {
            int operand = (IR[2] - '0') * 10 + (IR[3] - '0');
            for(int i = 0; i < 4; i++) M[operand][i] = R[i];
        }
        else if (IR[0] == 'C' && IR[1] == 'R') {
            int operand = (IR[2] - '0') * 10 + (IR[3] - '0');
            bool equal = true;
            for(int i = 0; i < 4; i++) {
                if (M[operand][i] != R[i]) {
                    equal = false;
                    break;
                }
            }
            C = equal;
        }
        else if (IR[0] == 'B' && IR[1] == 'T') {
            if (C) {
                IC = (IR[2] - '0') * 10 + (IR[3] - '0');
            }
        }
        else if (IR[0] == 'G' && IR[1] == 'D') {
            SI = 1;
            MOS();
        }
        else if (IR[0] == 'P' && IR[1] == 'D') {
            SI = 2;
            MOS();
        }
        else if (IR[0] == 'H') {
            SI = 3;
            MOS();
            break;
        }
    }
}

void START_EXECUTION() {
    IC = 0;
    EXECUTE_USER_PROGRAM();
}

void LOAD(string in_file, string out_file) {
    infile.open(in_file, ios::in);
    outfile.open(out_file, ios::out);
    
    string line;
    int m = 0;
    
    while (getline(infile, line)) {
        if (line.substr(0, 4) == "$AMJ") {
            INIT();
            m = 0;
        }
        else if (line.substr(0, 4) == "$DTA") {
            START_EXECUTION();
        }
        else if (line.substr(0, 4) == "$END") {
            continue;
        }
        else {
            int b = 0;
            for(int i = 0; i < 40; i++) buffer[i] = ' ';
            for(size_t i = 0; i < line.length() && i < 40; i++) {
                 buffer[i] = line[i];
            }

            b = 0;
            for (int i = 0; i < 10; i++) {
                for (int j = 0; j < 4; j++) {
                    M[m + i][j] = buffer[b++];
                }
            }
            m = m + 10;
        }
    }
    
    infile.close();
    outfile.close();
}

int main() {
    LOAD("input.txt", "output.txt");
    cout << "Phase 1 Simulation completed. Output written to output.txt." << endl;
    return 0;
}
