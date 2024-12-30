#include <iostream>
using namespace std;

#pragma region Memory Constants
// LC-3 supports 16bit addressing, so it supports 2^16 ~ 65536 memory locations
// and the word size (size of each memory location) is 16 bits (2 Bytes)
// Total memory size supported = 2^16 * 2B = 2^17 Bytes = 128KB
const int MEMORY_MAX = 1 << 16;

// Memory representation for this VM
uint16_t memory[MEMORY_MAX];
#pragma endregion Constants

#pragma region Registers
// LC-3 supports 8 general purpose registers and 2 special purpose registers - PC and COND
enum Register {
    R_R0 = 0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    R_PC, // Program Counter
    R_COND, // Condition Flag, used to set the result (Neg, 0, Pos) of last operation
    R_COUNT // Simple trick to track the total registers in the VM
};

// Tracks the registers of the VM
uint16_t registers[R_COUNT];

#pragma endregion Registers

#pragma region Opcodes
// An instruction in LC-3 is 16 bits long and it supports 16 different instructions.
// Instruction format: Opcode (4bits) | Parameters (12bits)
// An opcode represents an instruction that the VM CPU can run, eg ADD etc
enum Opcode {
    OP_BR = 0, // branch
    OP_ADD, // Add
    OP_LD, // Load
    OP_ST, // Store
    OP_JSR, // Jump register
    OP_AND, // bitwise AND operation
    OP_LDR, // load register
    OP_STR, // store register
    OP_RTI, // unused
    OP_NOT, // bitwise NOT
    OP_LDI, // Load indirect (inline memory loc)
    OP_STI, // store indirect
    OP_JMP, // Jump
    OP_RES, // reserved but unused
    OP_LEA, // Load address
    OP_TRAP, // trap
};

#pragma endregion Opcodes

#pragma region Condition Flag
// CPU uses condition flags to track the various situations, for LC-3 it uses a 3 bit
// flag which indicates the status of last executed computation.
enum COND {
    FL_POS = 1 << 0, // Positive
    FL_ZRO = 1 << 1, // Zero
    FL_NEG = 1 << 2, // Negative
};

#pragma endregion Condition Flag

int main() {
    cout << "LC-3 VM running..." << endl;
    return 0;
}