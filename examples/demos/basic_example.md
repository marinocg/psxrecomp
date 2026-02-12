# Example: Basic PSX Executable Structure

This example demonstrates a minimal PSX executable and how it would be recompiled.

## Original PSX Assembly

```asm
# Entry point at 0x80010000
.text
.globl _start

_start:
    # Initialize stack pointer
    lui $sp, 0x801F
    ori $sp, $sp, 0xFF00
    
    # Clear BSS section
    lui $a0, 0x8002      # BSS start
    lui $a1, 0x8003      # BSS end
    jal clear_memory
    nop
    
    # Call main function
    jal main
    nop
    
    # Infinite loop (should never reach here)
halt:
    j halt
    nop

clear_memory:
    # Loop to clear memory region
clear_loop:
    beq $a0, $a1, clear_done
    nop
    sw $zero, 0($a0)
    addiu $a0, $a0, 4
    j clear_loop
    nop
    
clear_done:
    jr $ra
    nop

main:
    # Simple main function
    addiu $sp, $sp, -8
    sw $ra, 0($sp)
    
    # Call a subroutine
    li $a0, 42
    jal some_function
    nop
    
    # Restore and return
    lw $ra, 0($sp)
    addiu $sp, $sp, 8
    jr $ra
    nop

some_function:
    # Double the input
    sll $v0, $a0, 1
    jr $ra
    nop
```

## Recompiled C++ Code

```cpp
#include "psxrecomp/runtime/psx_system.h"
#include <cstdint>

namespace game {

using namespace psxrecomp;

// Global PSX system
static runtime::PsxSystem* g_system = nullptr;

// PSX registers (emulated)
static u32 g_regs[32] = {0};

// Register access helpers
#define REG(n) g_regs[n]
#define ZERO 0
#define AT 1
#define V0 2
#define V1 3
#define A0 4
#define A1 5
#define SP 29
#define RA 31

// Function prototypes
void func_clear_memory();
void func_main();
void func_some_function();

// Entry point (0x80010000)
void start() {
    // Initialize stack pointer
    // lui $sp, 0x801F
    // ori $sp, $sp, 0xFF00
    REG(SP) = 0x801FFF00;
    
    // Clear BSS section
    // lui $a0, 0x8002
    // lui $a1, 0x8003
    REG(A0) = 0x80020000;
    REG(A1) = 0x80030000;
    
    // jal clear_memory
    func_clear_memory();
    
    // Call main function
    func_main();
    
    // Infinite loop
    while (true) {
        // halt: j halt
    }
}

// Clear memory function
void func_clear_memory() {
    // clear_loop:
    while (REG(A0) != REG(A1)) {
        // sw $zero, 0($a0)
        g_system->write<u32>(REG(A0), 0);
        
        // addiu $a0, $a0, 4
        REG(A0) += 4;
    }
    // jr $ra (return)
}

// Main function
void func_main() {
    // Save return address
    u32 saved_ra = REG(RA);
    
    // addiu $sp, $sp, -8
    REG(SP) -= 8;
    
    // sw $ra, 0($sp)
    g_system->write<u32>(REG(SP), saved_ra);
    
    // Call subroutine with argument 42
    // li $a0, 42
    REG(A0) = 42;
    
    func_some_function();
    
    // Restore stack
    // lw $ra, 0($sp)
    REG(RA) = g_system->read<u32>(REG(SP));
    
    // addiu $sp, $sp, 8
    REG(SP) += 8;
    
    // jr $ra (return handled by caller)
}

// Some function - doubles the input
void func_some_function() {
    // sll $v0, $a0, 1
    REG(V0) = REG(A0) << 1;
    
    // jr $ra (return)
}

// Game initialization
void initialize(runtime::PsxSystem* system) {
    g_system = system;
    
    // Initialize register state
    REG(ZERO) = 0;  // $0 is always zero
    
    // Start execution
    start();
}

} // namespace game
```

## Build System (CMakeLists.txt)

```cmake
cmake_minimum_required(VERSION 3.15)
project(RecompiledGame)

set(CMAKE_CXX_STANDARD 17)

# Find psxrecomp runtime library
find_package(psxrecomp REQUIRED)

add_executable(game
    recompiled_game.cpp
)

target_link_libraries(game
    psxrecomp::runtime
)
```

## Running the Recompiled Game

```cpp
// main.cpp - Host application
#include "psxrecomp/runtime/psx_system.h"
#include "recompiled_game.h"

int main(int argc, char* argv[]) {
    // Create PSX system
    psxrecomp::runtime::PsxSystem system;
    
    if (!system.initialize()) {
        fprintf(stderr, "Failed to initialize PSX system\n");
        return 1;
    }
    
    // Initialize and run game
    game::initialize(&system);
    
    // Main game loop
    while (true) {
        system.runFrame();
    }
    
    return 0;
}
```

## Notes

1. **Register Mapping**: PSX registers are emulated as a global array
2. **Memory Access**: All memory operations go through the runtime system
3. **Function Calls**: PSX `jal` instructions become C++ function calls
4. **Branch Delay Slots**: The instruction in the delay slot is moved before the branch in C++
5. **Stack Operations**: Stack is handled through the emulated SP register and runtime memory

This example shows the basic structure. Real games would have:
- Thousands of functions
- Hardware access (GPU, SPU, etc.)
- Dynamic memory allocation
- Complex control flow
- Interrupt handlers


## Troubleshooting Demo Black Screen / Missing EXE Magic

If the demo boots to a black screen and decompilation reports `Missing PS-X EXE signature`,
the extracted `*.EXE` often still contains raw CD sector wrappers.

Normalize the executable first:

```bash
python3 tools/psx_analyzer/normalize_psx_exe.py HELLOWLD.EXE -o HELLOWLD.fixed.EXE
```

Then use `HELLOWLD.fixed.EXE` as the decompiler input.
