#include "psxrecomp/runtime/kernel_events.h"

namespace psxrecomp
{
namespace runtime
{

KernelEventTable::KernelEventTable()
{
    reset();
}

void KernelEventTable::reset()
{
    for (auto& event : m_events)
    {
        event = KernelEvent{};
    }
}

u32 KernelEventTable::openEvent(u32 classId, u16 spec, EventMode mode, u32 callbackAddress)
{
    for (size_t i = 0; i < MAX_EVENTS; ++i)
    {
        if (m_events[i].status == EventStatus::Free)
        {
            m_events[i].classId = classId;
            m_events[i].spec = spec;
            m_events[i].mode = mode;
            m_events[i].callbackAddress = callbackAddress;
            m_events[i].status = EventStatus::Disabled;
            return HANDLE_BASE | static_cast<u32>(i << 4);
        }
    }
    return 0xFFFFFFFFu;
}

bool KernelEventTable::closeEvent(u32 handle)
{
    const size_t index = handleToIndex(handle);
    if (index >= MAX_EVENTS)
    {
        return false;
    }
    m_events[index] = KernelEvent{};
    return true;
}

bool KernelEventTable::enableEvent(u32 handle)
{
    const size_t index = handleToIndex(handle);
    if (index >= MAX_EVENTS)
    {
        return false;
    }
    auto& event = m_events[index];
    if (event.status == EventStatus::Free)
    {
        return false;
    }
    event.status = EventStatus::Enabled;
    return true;
}

bool KernelEventTable::disableEvent(u32 handle)
{
    const size_t index = handleToIndex(handle);
    if (index >= MAX_EVENTS)
    {
        return false;
    }
    auto& event = m_events[index];
    if (event.status == EventStatus::Free)
    {
        return false;
    }
    event.status = EventStatus::Disabled;
    return true;
}

u32 KernelEventTable::testEvent(u32 handle) const
{
    const size_t index = handleToIndex(handle);
    if (index >= MAX_EVENTS)
    {
        return 0;
    }
    const auto& event = m_events[index];
    if (event.status == EventStatus::Delivered)
    {
        // For NoCallback mode, reading resets to Enabled.
        // We use const_cast here because TestEvent has read-like semantics
        // but does mutate state on the real hardware.
        if (event.mode == EventMode::NoCallback)
        {
            const_cast<KernelEvent&>(event).status = EventStatus::Enabled;
        }
        return 1;
    }
    return 0;
}

void KernelEventTable::deliverEvent(u32 handle)
{
    const size_t index = handleToIndex(handle);
    if (index >= MAX_EVENTS)
    {
        return;
    }
    auto& event = m_events[index];
    if (event.status != EventStatus::Enabled)
    {
        return;
    }
    if (event.mode == EventMode::NoCallback)
    {
        event.status = EventStatus::Delivered;
    }
}

std::vector<u32> KernelEventTable::deliverByClassSpec(u32 classId, u16 spec)
{
    std::vector<u32> callbacks;
    for (auto& event : m_events)
    {
        if (event.status != EventStatus::Enabled)
        {
            continue;
        }
        if (event.classId != classId || event.spec != spec)
        {
            continue;
        }
        if (event.mode == EventMode::NoCallback)
        {
            event.status = EventStatus::Delivered;
            continue;
        }

        if (event.callbackAddress != 0)
        {
            callbacks.push_back(event.callbackAddress);
        }
    }
    return callbacks;
}

void KernelEventTable::undeliverEvent(u32 handle)
{
    const size_t index = handleToIndex(handle);
    if (index >= MAX_EVENTS)
    {
        return;
    }
    auto& event = m_events[index];
    if (event.status == EventStatus::Delivered)
    {
        event.status = EventStatus::Enabled;
    }
}

bool KernelEventTable::hasDeliveredEventForClassSpec(u32 classId, u16 spec) const
{
    for (const auto& event : m_events)
    {
        if (event.status == EventStatus::Delivered && event.classId == classId &&
            event.spec == spec)
        {
            return true;
        }
    }
    return false;
}

void KernelEventTable::undeliverByClassSpec(u32 classId, u16 spec)
{
    for (auto& event : m_events)
    {
        if (event.status == EventStatus::Delivered && event.classId == classId &&
            event.spec == spec)
        {
            event.status = EventStatus::Enabled;
        }
    }
}

const KernelEvent* KernelEventTable::getEvent(u32 handle) const
{
    const size_t index = handleToIndex(handle);
    if (index >= MAX_EVENTS)
    {
        return nullptr;
    }
    return &m_events[index];
}

bool KernelEventTable::isEventDelivered(u32 handle) const
{
    const size_t index = handleToIndex(handle);
    if (index >= MAX_EVENTS)
    {
        return false;
    }
    return m_events[index].status == EventStatus::Delivered;
}

bool KernelEventTable::consumeDeliveredEvent(u32 handle)
{
    const size_t index = handleToIndex(handle);
    if (index >= MAX_EVENTS)
    {
        return false;
    }

    auto& event = m_events[index];
    if (event.mode != EventMode::NoCallback || event.status != EventStatus::Delivered)
    {
        return false;
    }

    event.status = EventStatus::Enabled;
    return true;
}

u32 KernelEventTable::interruptLineToEventClass(InterruptLine line)
{
    switch (line)
    {
    case InterruptLine::VBlank:
        return EventClass::VBlank;
    case InterruptLine::Gpu:
        return EventClass::Gpu;
    case InterruptLine::Cdrom:
        return EventClass::Cdrom;
    case InterruptLine::Dma:
        return EventClass::Dma;
    case InterruptLine::Timer0:
        return EventClass::Timer0;
    case InterruptLine::Timer1:
        return EventClass::Timer1;
    case InterruptLine::Timer2:
        return EventClass::Timer2;
    case InterruptLine::Controller:
        return EventClass::Controller;
    case InterruptLine::Sio:
        return EventClass::Sio;
    case InterruptLine::Spu:
        return EventClass::Spu;
    case InterruptLine::Pio:
        return EventClass::Pio;
    default:
        return 0;
    }
}

size_t KernelEventTable::handleToIndex(u32 handle) const
{
    if ((handle & 0xFF000000u) != (HANDLE_BASE & 0xFF000000u))
    {
        return MAX_EVENTS;
    }
    const size_t index = (handle >> 4) & 0x1Fu;
    if (index >= MAX_EVENTS)
    {
        return MAX_EVENTS;
    }
    return index;
}

} // namespace runtime
} // namespace psxrecomp
