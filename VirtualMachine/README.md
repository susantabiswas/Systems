# LC-3 Virtual Machine
This project is an implementation of the LC-3 (Little Computer 3) virtual machine in C++. The LC-3 is a simple, computer architecture.


## Features
- Supports the full LC-3 instruction set
- Handles memory-mapped I/O for Keyboard input
- Includes a simple terminal interface for input and output
- Implements basic interrupt handling

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
```sh
g++ lc3_vm.cpp -o lc3
# ./lc3 <path to lc3 assembled binary file>
./lc3 2048.obj
```
### Output
```
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

## References
- A shorter version of [LC-3 ISA specification](https://www.jmeiners.com/lc3-vm/supplies/lc3-isa.pdf) hosted by https://www.jmeiners.com/lc3-vm/supplies/
- The [article](https://www.jmeiners.com/lc3-vm) that inspired this project.
- [2048.obj](https://www.jmeiners.com/lc3-vm/supplies/2048.obj) LC3 assessmbled binary from Meiners.
