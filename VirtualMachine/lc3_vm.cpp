#include <iostream>
#include <csignal>
#include <cstdint>
// IO, terminal console related to unix
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <sys/termios.h>
#include <sys/time.h>
#include <sys/mman.h>

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

enum MemoryRegister {
    MR_KBSR = 0xFE00, // Keyboard status register, can be used to check for keystrokes
    MR_KBDR = 0xFE02 // Keyboard data register, can be used to know the key that was pressed
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

#pragma region Trap code

// Trap codes are associated with piece of code called Trap routines. These
// are OS routines which the user level programs can use to achieve some functionality.
// Usually because these operations are high-priviledge ops, the cpu mode is changed to
// kernel mode and then the os runs the trap routine.
// In the context of this VM, these trap codes are mapped to the trap routines written for
// this VM alone, internally those functions might make system calls which will then invoke
// the OS Trap routines.
enum TrapCode {
    TRAP_GETC = 0x20, // Get a char from keyboard, no stdout echo
    TRAP_OUT = 0x21, // print a char
    TRAP_PUTS = 0x22, // print a word string
    TRAP_IN = 0x23, // get a char from keyboard, stdout echo
    TRAP_PUTSP = 0x24, //print a byte string
    TRAP_HALT = 0x25 // halt program execution
};

#pragma endregion Trap code

#pragma region VM utils

// This converts the big-endian data to little-endian
// NOTE: Only bytes as whole are reversed not individual bits
// eg 0x1234 -> 0x3412 (not 0x 4321) (byte1: 12 (0001 0010), byte2: 34 (0011 0100), in hexadecimal group of 4bits are used)
uint16_t swap_byte_layout16(uint16_t val) {
    return (val << 8) | (val >> 8);
}

void read_image_file(FILE* file) {
    // the LC3 machine code file starts with a 16-bit value that represents the starting address of the program
    // we will load the contents of the file into the memory starting from this address.
    uint16_t origin = 0;
    // read only the 1st line, this will give us the starting address of the program
    fread(&origin, sizeof(uint16_t), 1, file);

    // max no. of memory words that can be placed if we start from origin
    int max_lines = MEMORY_MAX - origin;

    // pos in memory where the file will be loaded
    uint16_t* file_ptr = memory + origin;

    // read the remaining data
    uint16_t lines_read = fread(file_ptr, sizeof(uint16_t), max_lines, file);

    // the lc3 machine code uses big-endian, so we will convert the data to little-endian
    // as our machine is little-endian
    for(int i = 0; i < lines_read; i++) {
        *file_ptr = swap_byte_layout16(*file_ptr);
        ++file_ptr;
    }

    cout << "Loaded image file into memory, size: " << lines_read * 2 << " Bytes" << endl;
}

bool load_image(const char* path) {
    cout << "Image path: " << path << endl;

    FILE* img_file = fopen(path, "rb");
    if (!img_file)
        return false;
    
    read_image_file(img_file);
    fclose(img_file);
    return true;
}

uint16_t check_keypress() {
    // set of file descp to check for read events
    fd_set readfds;
    FD_ZERO(&readfds); // init to empty set
    FD_SET(STDIN_FILENO, &readfds); // add stdin for tracking input event

    // set the polling properties for select operation
    // set the timeout (both sec and micro-sec) to 0, so that select will return immediately without blocking
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    // select will return the no. of file descp that have events
    // The select function monitors the file descriptors in the sets provided for readability, writability, and exceptional conditions.
    // the 1st arg of select is basically the max count + 1 till which the FD should be monitored, here we are
    // monitoring only stdin, so we set it to STDIN_FILENO + 1
    return select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);
}

uint16_t read_memory(uint16_t address) {
    // special case: if it is memory mapped KB status reg, then check for
    // any updated status for keyboard
    if (address == MR_KBSR) {
        // if there is a key press, set the KB status to 1
        if (check_keypress()) {
            memory[MR_KBSR] = (1 << 15); // MSB 1 indicating KB event
            memory[MR_KBDR] = getchar();
        }
        else {
            memory[MR_KBSR] = 0;
        }
    }

    return memory[address];
}

// saves the current terminal settings
struct termios original_tio;

void disable_input_buffering() {
    // save the terminal settings, which can be restored later
    tcgetattr(STDIN_FILENO, &original_tio);
    struct termios new_tio = original_tio;
    // c_lflag controls the various terminal functions,
    // disable canonical mode (line by line input) and input echo
    // with canonical disabled, the input is taken char by char
    new_tio.c_lflag &= ~ICANON & ~ECHO;

    // set the new terminal settings
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering() {
    // restore the orig terminal settings
    tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

void handle_interrupt(int signal) {
    // restore the terminal settings before exiting
    restore_input_buffering();
    cout << "Received signal: " << signal << endl;
    exit(-2);
}

#pragma endregion VM utils

int main(int argc, const char* argv[]) {
    if (argc < 2) {
        cout << "Usage: lc3 <image-file>\n";
        exit(2); 
    }

    if (!load_image(argv[1])) {
        cout << "LC3 image load failed\n";
        exit(1);
    }

    cout << "LC-3 VM running..." << endl;
    return 0;
}