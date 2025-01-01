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

uint16_t sign_extend_bits(uint16_t bit_count, uint16_t value) {
    // bit_count is the no. of bits in the value, it might be < 16 and hence
    // the remaining positions have to be filled depending on the sign of number.
    // if the MSB is 1, then the number is neg and should be filled
    // with 1s, else fill with 0s
    if ((value >> (bit_count - 1)) & 1)
        value |= (0xFFFF < bit_count);
    return value;
}

void update_cond_flag(uint16_t reg) {
    // COND register's value is updated based on the value of
    // last computation which is stored in the register
    uint16_t value = registers[reg];

    if (value == 0) 
        registers[R_COND] = FL_ZRO;
    else if (value >> 15) // if the MSB is 1, then it is a negative number
        registers[R_COND] = FL_NEG;
    else
        registers[R_COND] = FL_POS;
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

void memory_write(uint16_t data, uint16_t address) {
    memory[address] = data;
}

uint16_t memory_read(uint16_t address) {
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

void interrupt_handler(int signal) {
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

    // register the interrupt handler
    signal(SIGINT, interrupt_handler);
    // prepare the terminal
    disable_input_buffering();

    registers[R_COND] = FL_ZRO; // reset the condition flag
    registers[R_PC] = 0x3000; // start at the default 0x3000 mem addr

    cout << "LC-3 VM started..." << endl;
    bool run = true;

    // Instruction cycle control loop
    while(run) {
        // Instruction cycle: fetch, decode, execute

        // fetch the instr pointed by PC
        uint16_t instruction = memory_read(registers[R_PC]++);
        // decode and execute
        uint16_t opcode = instruction >> 12; // first 4 bits is opcode
        
        cout << opcode << " " << instruction << endl;
        switch (opcode) {
            case OP_ADD:
            {
                // Has 2 variants: ADD and ADD IMM
                // ADD DR, SR1, 0, 00, SR2; DR : SR1 + SR2
                // ADD DR, SR1, 1, IMM5; DR : SR1 + IMM5
                // get the 5th pos bit to decide the variant to use
                uint16_t imm_mode = (instruction >> 5) & 0x1;
                // destination register
                uint16_t dr = (instruction >> 9) & 0x7; // each operand is 3bits long
                // source register 1
                uint16_t sr1 = (instruction >> 6) & 0x7;

                if (imm_mode) {
                    // if immediate mode, then the last 5bits are the immediate value
                    // 0x1F = 0001 1111
                    // expand the immediate value to full 16bits
                    uint16_t imm5 = sign_extend_bits(5, instruction & 0x1F);
                    registers[dr] = registers[sr1] + imm5;
                }
                else {
                    // source register 2
                    uint16_t sr2 = (instruction & 0x7);
                    registers[dr] = registers[sr1] + registers[sr2];
                }

                update_cond_flag(dr);
                break;
            }
            case OP_AND:
            {
                // Has 2 variants: AND and AND IMM
                // AND DR, SR1, 0, 00, SR2; DR : SR1 AND SR2
                // AND DR, SR1, 1, IMM5; DR : SR1 AND IMM5
                // get the 5th pos bit to decide the variant to use
                uint16_t imm_mode = (instruction >> 5) & 0x1;
                // destination register
                uint16_t dr = (instruction >> 9) & 0x7; // each operand is 3bits long
                // source register 1
                uint16_t sr1 = (instruction >> 6) & 0x7;

                if (imm_mode) {
                    // if immediate mode, then the last 5bits are the immediate value
                    // 0x1F = 0001 1111
                    // expand the immediate value to full 16bits
                    uint16_t imm5 = sign_extend_bits(5, instruction & 0x1F);
                    registers[dr] = registers[sr1] & imm5;
                }
                else {
                    // source register 2
                    uint16_t sr2 = (instruction & 0x7);
                    registers[dr] = registers[sr1] & registers[sr2];
                }

                update_cond_flag(dr);
                break;
            }
            case OP_BR:
            {   // Branch
                // Checks the condition flag with condition register and branches to the PC offset if same
                // n|z|p|PCOffset(9b)
                uint16_t nzp = (instruction >> 9) & 0x7;
                uint16_t pc_offset = (instruction & 0x1FF);

                if (nzp & registers[R_COND])
                    registers[R_PC] += sign_extend_bits(9, pc_offset);
                break;
            }
            case OP_JMP:
            {   // Jump
                // Jump to the address stored in the base register
                // JMP 000 BaseR(3b) 000000; PC = BaseR
                uint16_t base_reg = (instruction >> 5) & 0x7;
                registers[R_PC] = registers[base_reg];
                break;
            }
            case OP_JSR:
            {   // Jump register
                // Save the current PC in R7 and then jump to the address depending on the variant
                registers[R_R7] = registers[R_PC];
                // Jump to the address stored in the PC offset
                // JSR: 1|PCOffset(11b); PC = PC + SIGNEXT(PCOffset)
                // Jump to the address stored in the base register
                // JSRR: 0|00|BaseR(3b)|PCOffset(6b); PC = BaseR
                
                // check the 11th bit to decide the variant
                uint16_t flag = (instruction >> 11) & 0x1;

                if (flag) // JSR
                    registers[R_PC] += sign_extend_bits(11, instruction &0x7FF);
                else // JSRR
                    registers[R_PC] = registers[(instruction >> 6) & 0x07];
                break;
            }
            case OP_LD:
            {   // Load
                // Load the value from the memory location to the destination register
                // LD DR(3b), PCOffset(9b); DR = mem[PC + SIGNEXT(PCOffset)]
                uint16_t dr = (instruction >> 9) & 0x7;
                uint16_t pc_offset = sign_extend_bits(9, instruction & 0x1FF);
                registers[dr] = memory_read(registers[R_PC] + pc_offset);
                update_cond_flag(dr);
                break;
            }
            case OP_LDI:
            {   // Load Indirect
                // LDI DR(3b), PCOffset(9b); DR = mem[mem[PC + SIGNEXT(PCOffset)]]
                uint16_t dr = (instruction >> 9) & 0x7;
                uint16_t pc_offset = sign_extend_bits(9, instruction & 0x1FF);
                registers[dr] = memory_read(memory_read(registers[R_PC] + pc_offset));
                update_cond_flag(dr);
                break;
            }
            case OP_LDR:
            {   // Load register
                // LDR DR(3b), BaseR(3b), Offset(6b); DR = mem[BaseR + Offset]
                uint16_t dr = (instruction >> 9) & 0x7;
                uint16_t base_r = (instruction >> 6) & 0x7;
                uint16_t offset = sign_extend_bits(6, instruction & 0x3F);

                registers[dr] = memory_read(registers[base_r] + offset);
                update_cond_flag(dr);
                break;
            }
            case OP_LEA:
            {   // Load effective address
                // LEA DR(3b), PCOffset(9b); DR = PC + SIGNEXT(PCOffset)
                uint16_t dr = (instruction >> 9) & 0x7;
                uint16_t pc_offset = sign_extend_bits(9, instruction & 0x1FF);
                registers[dr] = registers[R_PC] + pc_offset;
                update_cond_flag(dr);
                break;
            }
            case OP_NOT:
            {   // Bitwise not
                // NOT DR(3b), SR(3b), 1, 11111; DR = NOT SR
                uint16_t dr = (instruction >> 9) & 0x7;
                uint16_t sr = (instruction >> 6) & 0x7;
                registers[dr] = ~registers[sr];
                update_cond_flag(dr);
                break;
            }
            case OP_RES:
            {    // reserved, illegal opcode exception thrown
                break;
            }
            case OP_RTI:
            {
                // unused in the VM but in the actual LC-3 system it is 
                // used is used to return from an interrupt service routine (ISR)
                // and restore the previous processor state. In a real LC-3 machine, 
                // this involves switching from a privileged mode (used during the interrupt handling) 
                // back to the user mode and restoring the program counter (PC) and 
                // processor status register (PSR) from the stack.
                break;
            }
            case OP_ST:
            {   // Store
                // ST SR(3b), PCOffset(9b); mem[PC + SIGNEXT(PCOffset)] = SR
                uint16_t sr = (instruction >> 9) & 0x7;
                uint16_t pc_offset = sign_extend_bits(9, instruction & 0x1FF);
                memory_write(registers[sr], registers[R_PC] + pc_offset);
                break;
            }
            case OP_STI:
            {   // Store Indirect
                // STI SR(3b), PCOffset(9b); mem[mem[PC + SIGNEXT(PCOffset)]] = SR
                uint16_t sr = (instruction >> 9) & 0x7;
                uint16_t pc_offset = sign_extend_bits(9, instruction & 0x1FF);
                memory_write(registers[sr], memory_read(registers[R_PC] + pc_offset));
                break;
            }
            case OP_STR:
            {   // Store register
                // STR SR(3b), BaseR(3b), Offset(6b); mem[BaseR + SIGNEXT(PCOffset)] = SR
                uint16_t sr = (instruction >> 8) & 0x7;
                uint16_t base_r = (instruction >> 6) & 0x7;
                uint16_t offset = sign_extend_bits(6, instruction & 0x3F);
                memory_write(registers[sr], registers[base_r] + offset);
                break;
            }
            case OP_TRAP:
            {
                // Trap routines are used to perform high-privilege operations in the LC-3 system.
                // TRAP vector is 8 bits long, so the trap code is in the last 8 bits of the instruction
                // Usually the trap routines are saved in the memory and the trap vector (x0000 to x00FF (256 locs))
                // contains the starting memory location of each trap routine. When a trap instruction is executed, the PC is saved
                // and the trap routine number is used to fetch the routine's memory addr from the trap vector.
                // Eg TRAP x20 ; Directs the operating system to execute the GETC system call.
                // ; The starting address of this system call is contained in memory location x0020.

                // TRAP: 1111 0000 | trapvect8(8b)
                // save the PC in R7 first before jumping to trap routine
                registers[R_R7] = registers[R_PC];
                // get the trap code from the last 8 bits (trapvect8)
                uint16_t trap_code = instruction & 0x1FF;

                switch (trap_code) {
                    case TRAP_GETC:
                    {
                        // read a single char from the keyboard and store it in R0
                        registers[R_R0] = (uint16_t)getchar();
                        update_cond_flag(R_R0);
                        break;
                    }
                    case TRAP_OUT:
                    {
                        // write a single char to the console
                        putc((char)registers[R_R0], stdout);
                        fflush(stdout);
                        break;
                    }
                    case TRAP_PUTS:
                    {   
                        // write a word string (ASCII chars) to the console, starting addr
                        // is stored in R0, writing terminates when NULL (x0000) char is encountered
                        uint16_t str_ptr = memory + registers[R_R0];
                        while (*str_ptr) {
                            putc((char)*str_ptr, stdout);
                            ++str_ptr;
                        }
                        fflush(stdout);
                        break;
                    }
                    case TRAP_IN:
                    {
                        break;
                    }
                    case TRAP_PUTSP:
                    {
                        break;
                    }
                    case TRAP_HALT:
                    {
                        break;
                    }
                    default:
                    {
                        break;
                    }
                }
                break;
            }
            default:
            {
                abort();
                break;
            }
        }
    }

    restore_input_buffering();
    return 0;
}