/**
 * @file kernel_events_test.cpp
 * @brief Tests for KernelEventTable: handle lifecycle, delivery, callback
 *        queue ordering, and interrupt-line-to-class mapping.
 */
#include "psxrecomp/runtime/kernel_events.h"

#include <cassert>
#include <iostream>
#include <vector>

int main()
{
    using psxrecomp::u32;
    namespace EventClass = psxrecomp::runtime::EventClass;
    namespace EventSpec = psxrecomp::runtime::EventSpec;
    using psxrecomp::runtime::EventMode;
    using psxrecomp::runtime::EventStatus;
    using psxrecomp::runtime::InterruptLine;
    using psxrecomp::runtime::KernelEvent;
    using psxrecomp::runtime::KernelEventTable;

    // ---------------------------------------------------------------
    // Test 1: Fresh table has all free slots
    // ---------------------------------------------------------------
    {
        KernelEventTable table;
        for (size_t i = 0; i < KernelEventTable::MAX_EVENTS; ++i)
        {
            u32 handle = KernelEventTable::HANDLE_BASE | static_cast<u32>(i << 4);
            const auto* ev = table.getEvent(handle);
            assert(ev != nullptr);
            assert(ev->status == EventStatus::Free);
        }
        std::cerr << "[PASS] fresh table has all free slots\n";
    }

    // ---------------------------------------------------------------
    // Test 2: OpenEvent allocates a slot and returns a valid handle
    // ---------------------------------------------------------------
    {
        KernelEventTable table;
        u32 handle =
            table.openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::NoCallback, 0);
        assert(handle != 0xFFFFFFFFu);
        const auto* ev = table.getEvent(handle);
        assert(ev != nullptr);
        assert(ev->classId == EventClass::VBlank);
        assert(ev->spec == EventSpec::Counter);
        assert(ev->mode == EventMode::NoCallback);
        assert(ev->status == EventStatus::Disabled);
        assert(ev->callbackAddress == 0);
        std::cerr << "[PASS] OpenEvent allocates slot\n";
    }

    // ---------------------------------------------------------------
    // Test 3: OpenEvent exhausts slots
    // ---------------------------------------------------------------
    {
        KernelEventTable table;
        for (size_t i = 0; i < KernelEventTable::MAX_EVENTS; ++i)
        {
            u32 handle =
                table.openEvent(EventClass::Gpu, EventSpec::Interrupted, EventMode::NoCallback, 0);
            assert(handle != 0xFFFFFFFFu);
        }
        // Next should fail
        u32 overflow =
            table.openEvent(EventClass::Gpu, EventSpec::Interrupted, EventMode::NoCallback, 0);
        assert(overflow == 0xFFFFFFFFu);
        std::cerr << "[PASS] OpenEvent exhausts after MAX_EVENTS\n";
    }

    // ---------------------------------------------------------------
    // Test 4: CloseEvent frees a slot
    // ---------------------------------------------------------------
    {
        KernelEventTable table;
        u32 handle = table.openEvent(EventClass::Dma, EventSpec::EndOfIO, EventMode::NoCallback, 0);
        assert(handle != 0xFFFFFFFFu);
        assert(table.closeEvent(handle));
        const auto* ev = table.getEvent(handle);
        assert(ev != nullptr);
        assert(ev->status == EventStatus::Free);
        std::cerr << "[PASS] CloseEvent frees slot\n";
    }

    // ---------------------------------------------------------------
    // Test 5: EnableEvent / DisableEvent transitions
    // ---------------------------------------------------------------
    {
        KernelEventTable table;
        u32 handle =
            table.openEvent(EventClass::Timer0, EventSpec::Counter, EventMode::NoCallback, 0);
        // Initial is Disabled
        assert(table.getEvent(handle)->status == EventStatus::Disabled);
        assert(table.enableEvent(handle));
        assert(table.getEvent(handle)->status == EventStatus::Enabled);
        assert(table.disableEvent(handle));
        assert(table.getEvent(handle)->status == EventStatus::Disabled);
        std::cerr << "[PASS] Enable/Disable transitions\n";
    }

    // ---------------------------------------------------------------
    // Test 6: DeliverEvent on enabled event sets Delivered status
    // ---------------------------------------------------------------
    {
        KernelEventTable table;
        u32 handle =
            table.openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::NoCallback, 0);
        table.enableEvent(handle);
        table.deliverEvent(handle);
        assert(table.getEvent(handle)->status == EventStatus::Delivered);
        std::cerr << "[PASS] DeliverEvent sets Delivered\n";
    }

    // ---------------------------------------------------------------
    // Test 7: DeliverEvent on disabled event is a no-op
    // ---------------------------------------------------------------
    {
        KernelEventTable table;
        u32 handle =
            table.openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::NoCallback, 0);
        // Not enabled
        table.deliverEvent(handle);
        assert(table.getEvent(handle)->status == EventStatus::Disabled);
        std::cerr << "[PASS] DeliverEvent on disabled is no-op\n";
    }

    // ---------------------------------------------------------------
    // Test 8: TestEvent returns 1 on Delivered, 0 otherwise
    // ---------------------------------------------------------------
    {
        KernelEventTable table;
        u32 handle =
            table.openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::NoCallback, 0);
        table.enableEvent(handle);
        assert(table.testEvent(handle) == 0);
        table.deliverEvent(handle);
        assert(table.testEvent(handle) == 1);
        // After test, NoCallback events reset to Enabled
        assert(table.getEvent(handle)->status == EventStatus::Enabled);
        // Now should return 0 again
        assert(table.testEvent(handle) == 0);
        std::cerr << "[PASS] TestEvent return values and reset\n";
    }

    // ---------------------------------------------------------------
    // Test 9: Callback-mode DeliverEvent remains enabled
    // ---------------------------------------------------------------
    {
        KernelEventTable table;
        u32 handle = table.openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::Callback,
                                     0x80010100);
        table.enableEvent(handle);
        table.deliverEvent(handle);
        // Callback mode is edge-triggered through callback invocation and does
        // not use the polling Delivered state.
        assert(table.testEvent(handle) == 0);
        assert(table.getEvent(handle)->status == EventStatus::Enabled);
        std::cerr << "[PASS] Callback-mode DeliverEvent stays Enabled\n";
    }

    // ---------------------------------------------------------------
    // Test 10: UndeliverEvent resets Delivered polling events to Enabled
    // ---------------------------------------------------------------
    {
        KernelEventTable table;
        u32 handle =
            table.openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::NoCallback, 0);
        table.enableEvent(handle);
        table.deliverEvent(handle);
        assert(table.getEvent(handle)->status == EventStatus::Delivered);
        table.undeliverEvent(handle);
        assert(table.getEvent(handle)->status == EventStatus::Enabled);
        std::cerr << "[PASS] UndeliverEvent resets polling event to Enabled\n";
    }

    // ---------------------------------------------------------------
    // Test 11: deliverByClassSpec delivers all matching events
    // ---------------------------------------------------------------
    {
        KernelEventTable table;
        u32 h1 = table.openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::Callback,
                                 0x80010300);
        u32 h2 = table.openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::Callback,
                                 0x80010400);
        u32 h3 =
            table.openEvent(EventClass::Gpu, EventSpec::Counter, EventMode::Callback, 0x80010500);
        table.enableEvent(h1);
        table.enableEvent(h2);
        table.enableEvent(h3);

        auto callbacks = table.deliverByClassSpec(EventClass::VBlank, EventSpec::Counter);
        assert(callbacks.size() == 2);
        assert(callbacks[0] == 0x80010300);
        assert(callbacks[1] == 0x80010400);
        // h3 should not be delivered
        assert(table.getEvent(h3)->status == EventStatus::Enabled);
        std::cerr << "[PASS] deliverByClassSpec delivers matching events\n";
    }

    // ---------------------------------------------------------------
    // Test 12: deliverByClassSpec skips disabled events
    // ---------------------------------------------------------------
    {
        KernelEventTable table;
        u32 h1 = table.openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::Callback,
                                 0x80010300);
        u32 h2 = table.openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::Callback,
                                 0x80010400);
        table.enableEvent(h1);
        // h2 stays disabled

        auto callbacks = table.deliverByClassSpec(EventClass::VBlank, EventSpec::Counter);
        assert(callbacks.size() == 1);
        assert(callbacks[0] == 0x80010300);
        (void)h2;
        std::cerr << "[PASS] deliverByClassSpec skips disabled\n";
    }

    // ---------------------------------------------------------------
    // Test 13: Callback events can be delivered repeatedly
    // ---------------------------------------------------------------
    {
        KernelEventTable table;
        u32 handle = table.openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::Callback,
                                     0x80010600);
        table.enableEvent(handle);

        auto callbacks1 = table.deliverByClassSpec(EventClass::VBlank, EventSpec::Counter);
        auto callbacks2 = table.deliverByClassSpec(EventClass::VBlank, EventSpec::Counter);
        assert(callbacks1.size() == 1);
        assert(callbacks2.size() == 1);
        assert(callbacks1[0] == 0x80010600);
        assert(callbacks2[0] == 0x80010600);
        assert(table.getEvent(handle)->status == EventStatus::Enabled);
        std::cerr << "[PASS] callback events remain enabled across deliveries\n";
    }

    // ---------------------------------------------------------------
    // Test 14: NoCallback events in deliverByClassSpec return no callbacks
    // ---------------------------------------------------------------
    {
        KernelEventTable table;
        u32 h1 = table.openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::NoCallback, 0);
        table.enableEvent(h1);

        auto callbacks = table.deliverByClassSpec(EventClass::VBlank, EventSpec::Counter);
        assert(callbacks.empty());
        // But the event should still be Delivered
        assert(table.getEvent(h1)->status == EventStatus::Delivered);
        std::cerr << "[PASS] NoCallback events delivered but no callbacks returned\n";
    }

    // ---------------------------------------------------------------
    // Test 15: Invalid handle operations
    // ---------------------------------------------------------------
    {
        KernelEventTable table;
        assert(!table.closeEvent(0xDEADBEEF));
        assert(!table.enableEvent(0x00000000));
        assert(!table.disableEvent(0x12345678));
        assert(table.testEvent(0xFFFFFFFF) == 0);
        assert(table.getEvent(0xBADCAFE0) == nullptr);
        std::cerr << "[PASS] invalid handle operations\n";
    }

    // ---------------------------------------------------------------
    // Test 16: Reset clears all events
    // ---------------------------------------------------------------
    {
        KernelEventTable table;
        table.openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::Callback, 0x80010100);
        table.reset();
        for (size_t i = 0; i < KernelEventTable::MAX_EVENTS; ++i)
        {
            u32 handle = KernelEventTable::HANDLE_BASE | static_cast<u32>(i << 4);
            assert(table.getEvent(handle)->status == EventStatus::Free);
        }
        std::cerr << "[PASS] reset clears all events\n";
    }

    // ---------------------------------------------------------------
    // Test 17: interruptLineToEventClass mapping
    // ---------------------------------------------------------------
    {
        assert(EventClass::Timer2 == EventClass::Timer1);
        assert(EventClass::Controller == 0xF0000008u);
        assert(EventClass::Spu == 0xF0000009u);
        assert(EventClass::Pio == 0xF000000Au);
        assert(EventClass::Sio == 0xF000000Bu);

        assert(KernelEventTable::interruptLineToEventClass(InterruptLine::VBlank) ==
               EventClass::VBlank);
        assert(KernelEventTable::interruptLineToEventClass(InterruptLine::Gpu) == EventClass::Gpu);
        assert(KernelEventTable::interruptLineToEventClass(InterruptLine::Cdrom) ==
               EventClass::Cdrom);
        assert(KernelEventTable::interruptLineToEventClass(InterruptLine::Dma) == EventClass::Dma);
        assert(KernelEventTable::interruptLineToEventClass(InterruptLine::Timer0) ==
               EventClass::Timer0);
        assert(KernelEventTable::interruptLineToEventClass(InterruptLine::Timer1) ==
               EventClass::Timer1);
        assert(KernelEventTable::interruptLineToEventClass(InterruptLine::Timer2) ==
               EventClass::Timer2);
        assert(KernelEventTable::interruptLineToEventClass(InterruptLine::Controller) ==
               EventClass::Controller);
        assert(KernelEventTable::interruptLineToEventClass(InterruptLine::Sio) == EventClass::Sio);
        assert(KernelEventTable::interruptLineToEventClass(InterruptLine::Spu) == EventClass::Spu);
        assert(KernelEventTable::interruptLineToEventClass(InterruptLine::Pio) == EventClass::Pio);
        std::cerr << "[PASS] interruptLineToEventClass mapping\n";
    }

    // ---------------------------------------------------------------
    // Test 18: Handle encoding/decoding round-trip
    // ---------------------------------------------------------------
    {
        KernelEventTable table;
        std::vector<u32> handles;
        for (size_t i = 0; i < 5; ++i)
        {
            u32 h =
                table.openEvent(EventClass::VBlank, EventSpec::Counter, EventMode::NoCallback, 0);
            handles.push_back(h);
        }
        for (size_t i = 0; i < handles.size(); ++i)
        {
            const auto* ev = table.getEvent(handles[i]);
            assert(ev != nullptr);
            assert(ev->classId == EventClass::VBlank);
        }
        // Close the middle one and reopen — should reuse slot
        table.closeEvent(handles[2]);
        u32 reused = table.openEvent(EventClass::Gpu, EventSpec::Interrupted, EventMode::Callback,
                                     0x80020000);
        assert(reused != 0xFFFFFFFFu);
        assert(table.getEvent(reused)->classId == EventClass::Gpu);
        std::cerr << "[PASS] handle encoding/decoding round-trip\n";
    }

    std::cerr << "All kernel_events tests passed.\n";
    return 0;
}
