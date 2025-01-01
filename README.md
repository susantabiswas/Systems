# Bytecode Virtual Machine: LC-3 Virtual Machine
This project is an implementation of the LC-3 virtual machine in C++.

Here is how a virtual machine fits into the picture. We can design a standard virtual machine architecture for the programming language. Irrespective of the platform, the compiler and interpreter only need to generate bytecode for that standard VM architecture (No need to have explicit versions of the code base for ARM, x86, Windows, etc). Then the Virtual machine runs that bytecode and executes the program.
```
+---------------------+
|  Source Code (C,    |
|  Python, Java, etc.)|
+----------+----------+
           |
           v
+----------+----------+
|   Compiler/Interpreter|
|  (Converts to Bytecode|
|   or Machine Code)   |
+----------+----------+
           |
           v
+----------+----------+
|  Bytecode/Machine   |
|  Code               |
+----------+----------+
           |
           v
+----------+----------+
|  Virtual Machine    |
|  (Executes Bytecode)|
+----------+----------+
           |
           v
+----------+----------+
|  Hardware (CPU,     |
|  Memory, etc.)      |
+---------------------+
```

### Capabilities of this VM
- **16-bit Addressing**: Supports 2^16 (65,536) memory locations, each 16 bits wide.
- **Registers**: Includes 8 general-purpose registers (R0-R7) and 2 special-purpose registers (PC and COND).
- **Instruction Set**: Supports 16 different instructions, including arithmetic operations, memory access, control flow, and trap routines.
- **Memory-Mapped I/O**: Includes keyboard status and data registers for handling input.
- **Condition Flags**: Uses 3 condition flags (Positive, Zero, Negative) to track the status of the last executed computation.
- **Trap Routines**: Provides system calls for input/output operations and program control.


## Prerequisites
- C++11 or later
- Unix / Linux

## Getting Started

### Clone the Repository

```sh
git clone https://github.com/susantabiswas/Systems.git
cd Systems/VirtualMachine/
```

### Compile & Run
We can run LC-3 machine code on this VM, we will use an ASCII version of the 2048 console game and run on this VM.
```sh
g++ lc3_vm.cpp -o lc3
# ./lc3 <path to lc3 assembled binary file>
# you can use the sample 2048.obj file provided in assets
./lc3 assets/2048.obj
```
### Output

```
Control the game using WASD keys.
Are you on an ANSI terminal (y/n)?
+--------------------------+
|                          |
|                     2    |
|                          |
|                     2    |
|                          |
+--------------------------+
|                          |
|                          |
|                          |
|   2     8     2          |
|                          |
|   4     16    8          |
|                          |
|   8     2     4     2    |
|                          |
+--------------------------+
Received signal: 2
```

![2048](assets//2048-lc3.gif)

## Deep Dive
The VM simulates the behavior of the LC-3 CPU, including instruction fetch, decode, and execute cycles. Below is a summary of the key components and flow of the VM:

### Architecture
```
+---------------------+
|       Memory        |
|---------------------|
|  Address Space:     |
|  0x0000 - 0xFFFF    |
+---------------------+
           |
           v
+---------------------+
|    Program Counter  |
|        (PC)         |
+---------------------+
           |
           v
+---------------------+
|  Instruction Register|
|        (IR)         |
+---------------------+
           |
           v
+---------------------+
|  Control Unit       |
+---------------------+
           |
           v
+---------------------+
|  Arithmetic Logic   |
|       Unit (ALU)    |
+---------------------+
           |
           v
+---------------------+
|  General Purpose    |
|     Registers       |
|  R0, R1, R2, R3,    |
|  R4, R5, R6, R7     |
+---------------------+
           |
           v
+---------------------+
|  Condition Flags    |
|  (Positive, Zero,   |
|   Negative)         |
+---------------------+
           |
           v
+---------------------+
|  Input/Output       |
|  (Keyboard, Display)|
+---------------------+
```

## Memory Map
```
+---------------------+
|       Memory        |
| 0000 - Trap Vector  |
|        Table        |
| 00FF                |
| 0100 - Interrupt    |
|        Vector Table |
| 01FF                |
| 0200 - OS & Kernel  |
|        Stack        |
| 2FFF                |
| 3000 - User Program |
| FDFF                |
| FFE0 - Memory Mapped|
|        Registers    |
| FFFF                |
+---------------------+
|     Memory Map      |
+---------------------+
```

### Key Components
1. **Registers**: The VM uses an array of 10 registers, including 8 general-purpose registers (R0-R7), the program counter (R_PC), and the condition flags register (R_COND).
2. **Memory**: The VM has a memory array to simulate the LC-3's memory space. It supports 16-bit address space and effectivly has ~65k memory locations and can support upto 128KB of memory.
3. **Condition Flags**: The VM uses condition flags (Positive, Zero, Negative) to track the status of the last executed computation.
4. **Instruction Set**: The VM supports various LC-3 instructions which are 16bits long.

```
+---------------------+
| Instruction Fetch: 
| Load from memory  
+---------------------+
           |
           v
+---------------------+
| Instruction Decode: 
| Parse the byte instr 
| and take out opcode
+---------------------+
           |
           v
+---------------------+
| Instruction Execute:
| Eval using opcode 
+---------------------+
           |
           v
+---------------------+
| Update PC & Flags   |
+---------------------+
           |
           v
+---------------------+
| Next Instruction    |
+---------------------+
```
This diagram shows the cyclic nature of the instruction fetch-decode-execute process in the VM. Each instruction updates the PC and condition flags before moving to the next instruction.

### Flow Summary
- The VM loads the assembly binary and using the 1st line, determines the address where the file's data should be placed. This is usually 0x3000 for LC-3 programs. So the entire file's binary data is then placed contigously in the memory starting 0x3000. 
- After that each 16-bit instruction undergoes the extract, decode and execute cycle. 
  `Note: LC-3 uses big-endian encoding against the little-endian encoding that most of today's computers run on, so it is essential to convert the big-endian word to little-endian before doing anything.`
- 
  - Instruction Fetch: The VM fetches the next instruction from memory using the program counter (PC).
  - Instruction Decode: The fetched instruction is decoded to determine the operation to be performed.
  - Instruction Execute: Based on the decoded operation, the VM executes the corresponding instruction.
  - Update PC & Flags: The VM updates the program counter (PC) and condition flags based on the result of the executed instruction.
- Next Instruction: The VM moves to the next instruction and repeats the cycle.

#### Terminal Settings Related Methods
In the LC-3 virtual machine, terminal settings related methods are used to handle input and output operations, particularly for managing the terminal's behavior during execution. These methods are essential for simulating the LC-3's interaction with the user and ensuring smooth operation of the VM. Here are the key methods and their purposes:

`disable_input_buffering():`
Disables the terminal's input buffering and echoing.
This ensures that input characters are immediately available to the VM without waiting for the Enter key, which is crucial for real-time input handling.

`restore_input_buffering():`
Restores the terminal's input buffering and echoing to their default settings.
This ensures that the terminal behaves normally after the VM has finished executing

`handle_interrupt(int signal):`
This ensures that the VM can clean up resources and restore terminal settings before exiting, providing a better user experience and preventing terminal misbehavior.




## References
- A shorter version of [LC-3 specification](https://www.jmeiners.com/lc3-vm/supplies/lc3-isa.pdf) hosted by https://www.jmeiners.com/lc3-vm/supplies/
- The [article](https://www.jmeiners.com/lc3-vm) that inspired this project.
- [2048.obj](https://github.com/rpendleton/lc3-2048) LC3 assembly from https://github.com/rpendleton/lc3-2048.
