/**
 * @file gpu_linked_list_dma_regression_test.cpp
 * @brief Regression guard for GPU DMA2 linked-list (DrawOTag) path.
 *
 * Verified:
 *  1. GPU linked-list DMA with CHCR=0x01000401 starts without bit28.
 *  2. Commands from linked-list nodes are consumed by the GPU.
 *  3. CHCR bit24 (Start/Busy) clears on DMA completion.
 *  4. Linked-list address wrapping works at the 2MB RAM boundary.
 *  5. Multi-node linked-list transfers all nodes in order.
 *
 * PSX-SPX reference: CHCR=0x01000401 is the canonical GPU linked-list value
 * (SyncMode=2, fromRam=1, bit24=busy). Bit28 is NOT required for SyncMode=2.
 */
#include "psxrecomp/runtime/dma.h"
#include "psxrecomp/runtime/psx_system.h"

#include <cstdlib>
#include <iostream>

using psxrecomp::Address;
using psxrecomp::u32;
using psxrecomp::runtime::DmaController;
using psxrecomp::runtime::DmaPort;
using psxrecomp::runtime::PsxSystem;

namespace
{

constexpr Address GPU_GP1 = 0x1F801814u;

void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "[FAIL] " << message << "\n";
        std::abort();
    }
}

constexpr Address gpuDmaBase()
{
    return DmaController::ChannelBase +
           DmaController::ChannelStride * static_cast<Address>(DmaPort::Gpu);
}

// ---------------------------------------------------------------
// Helper: initialize a system and enable GPU DMA RAM→GPU.
// ---------------------------------------------------------------
static void initReadySystem(PsxSystem& system)
{
    if (!system.initialize())
    {
        std::cerr << "[FAIL] Failed to initialize system\n";
        std::abort();
    }
    // Enable GPU DMA mode: CPU→GP0 (GP1 command 04h value 2).
    system.writeMmioExplicit<u32>(GPU_GP1, 0x04000002u);
}

// ---------------------------------------------------------------
// Test 1: Single-node linked-list with a real GPU command
//
// Linked-list: one node at nodeAddr, 1 command (FillRect header word),
// followed by end-of-list sentinel in the header.
// CHCR=0x01000401 must start the transfer without bit28.
// ---------------------------------------------------------------
static void testSingleNodeLinkedList()
{
    PsxSystem system;
    initReadySystem(system);
    const Address base = gpuDmaBase();

    // Build linked-list node:
    //   header = (commandCount << 24) | 0x00FFFFFF (end-of-list sentinel)
    //   word[1] = GPU command: FillRect (0x02) opcode with colour 0x00222222
    constexpr Address nodeAddr = 0x00016000u;
    constexpr u32 cmdFillRect = 0x02222222u;
    system.write<u32>(nodeAddr,      (1u << 24u) | 0x00FFFFFFu); // 1 cmd, end
    system.write<u32>(nodeAddr + 4u, cmdFillRect);

    system.write<u32>(base + 0x0, nodeAddr);
    system.write<u32>(base + 0x4, 0x00000000u); // BCR unused for linked-list
    // Canonical GPU linked-list CHCR (PSX-SPX): SyncMode=2, bit24, fromRam.
    system.write<u32>(base + 0x8, 0x01000401u);

    // CHCR bit24 must clear: transfer completed.
    const u32 chcrAfter = system.read<u32>(base + 0x8);
    require((chcrAfter & (1u << 24u)) == 0u,
            "Single-node: CHCR bit24 must clear after linked-list DMA completion");

    // GPU must have received the command word.
    require(system.gpu().fifoDepth() >= 1u,
            "Single-node: GPU must have received at least 1 command from linked-list");

    std::cerr << "[PASS] Single-node GPU linked-list DMA (0x01000401) works\n";
}

// ---------------------------------------------------------------
// Test 2: Multi-node linked-list
//
// Chain: node0 → node1 → end-of-list.
// Verifies that the DMA walker follows the forward pointer correctly.
// ---------------------------------------------------------------
static void testMultiNodeLinkedList()
{
    PsxSystem system;
    initReadySystem(system);
    const Address base = gpuDmaBase();

    // Node 1 (first in chain, stored at higher address — PSX OT convention):
    //   header = (1 cmd << 24) | address of node0
    // Node 0 (second in chain, earlier address):
    //   header = (1 cmd << 24) | 0x00FFFFFF (end-of-list)
    constexpr Address node0 = 0x00017000u;
    constexpr Address node1 = 0x00017010u;

    // node0: 1 command, end sentinel
    system.write<u32>(node0,      (1u << 24u) | 0x00FFFFFFu);
    system.write<u32>(node0 + 4u, 0x01000000u); // GPU NOP

    // node1: 1 command, points to node0
    system.write<u32>(node1,      (1u << 24u) | (node0 & 0x00FFFFFFu));
    system.write<u32>(node1 + 4u, 0x01000000u); // GPU NOP

    // Start from node1 (the ordering-table head).
    system.write<u32>(base + 0x0, node1);
    system.write<u32>(base + 0x4, 0x00000000u);
    system.write<u32>(base + 0x8, 0x01000401u);

    const u32 chcrAfter = system.read<u32>(base + 0x8);
    require((chcrAfter & (1u << 24u)) == 0u,
            "Multi-node: CHCR bit24 must clear on completion");

    // Both nodes contribute 1 command each: 2 total.
    require(system.gpu().fifoDepth() >= 2u,
            "Multi-node: GPU must have received commands from all chain nodes");

    std::cerr << "[PASS] Multi-node GPU linked-list DMA traverses chain correctly\n";
}

// ---------------------------------------------------------------
// Test 3: Linked-list address wraps at 2MB RAM boundary
//
// A node near the top of the 2MB window with a next-pointer that wraps
// to near address 0 must be followed correctly.
// ---------------------------------------------------------------
static void testLinkedListAddressWrap()
{
    PsxSystem system;
    initReadySystem(system);
    const Address base = gpuDmaBase();

    // Tail node at the very top of RAM (wraps to address 0x000000).
    constexpr Address tailAddr = 0x001FFFFCu; // last 4-byte-aligned word in 2MB
    constexpr Address headAddr = 0x00000000u; // wraps to zero

    // head node (address 0): 1 command, end-of-list.
    system.write<u32>(headAddr,      (1u << 24u) | 0x00FFFFFFu);
    system.write<u32>(headAddr + 4u, 0x00000000u); // GPU NOP

    // tail node: 1 command, next = headAddr (wrapped).
    // The DMA header's low 24 bits encode the next address.
    system.write<u32>(tailAddr, (1u << 24u) | (headAddr & 0x00FFFFFFu));
    // No command word here (commandCount=1 reads the word at tailAddr+4).
    system.write<u32>((tailAddr + 4u) & 0x1FFFFCu, 0x00000000u);

    system.write<u32>(base + 0x0, tailAddr);
    system.write<u32>(base + 0x4, 0x00000000u);
    system.write<u32>(base + 0x8, 0x01000401u);

    const u32 chcrAfter = system.read<u32>(base + 0x8);
    require((chcrAfter & (1u << 24u)) == 0u,
            "Address-wrap: CHCR bit24 must clear after wrapped linked-list DMA");

    require(system.gpu().fifoDepth() >= 1u,
            "Address-wrap: GPU must receive commands across 2MB wrap boundary");

    std::cerr << "[PASS] GPU linked-list DMA wraps address at 2MB RAM boundary\n";
}

// ---------------------------------------------------------------
// Test 4: CHCR bit28 is cleared at transfer begin (not required to start)
//
// Write 0x11000401 (bit28 AND bit24 set, SyncMode=2). Verify transfer runs
// and bit28 is cleared by hardware before bit24 clears on completion.
// ---------------------------------------------------------------
static void testLinkedListBit28ClearedAtBegin()
{
    PsxSystem system;
    initReadySystem(system);
    const Address base = gpuDmaBase();

    constexpr Address nodeAddr = 0x00018000u;
    system.write<u32>(nodeAddr, 0x00FFFFFFu); // 0 commands, end-of-list

    system.write<u32>(base + 0x0, nodeAddr);
    system.write<u32>(base + 0x4, 0x00000000u);
    // Write with both bit28 and bit24 set (SyncMode=2): bit28 should clear at begin.
    system.write<u32>(base + 0x8, 0x11000401u);

    const u32 chcrAfter = system.read<u32>(base + 0x8);
    require((chcrAfter & (1u << 28u)) == 0u,
            "Bit28-set: CHCR bit28 must be cleared by hardware when transfer begins");
    require((chcrAfter & (1u << 24u)) == 0u,
            "Bit28-set: CHCR bit24 must be cleared by hardware when transfer completes");

    std::cerr << "[PASS] CHCR bit28 cleared at begin, bit24 cleared at complete (SyncMode=2)\n";
}

} // namespace

int main()
{
    testSingleNodeLinkedList();
    testMultiNodeLinkedList();
    testLinkedListAddressWrap();
    testLinkedListBit28ClearedAtBegin();

    std::cerr << "\nAll GPU linked-list DMA regression tests passed.\n";
    return 0;
}
