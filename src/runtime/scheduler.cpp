#include "psxrecomp/runtime/scheduler.h"

#include <algorithm>
#include <utility>

namespace psxrecomp
{
namespace runtime
{

Scheduler::EventId Scheduler::schedule(Time cyclesFromNow, Callback callback)
{
    const EventId id = m_nextId++;
    Event event;
    event.id = id;
    event.time = m_now + cyclesFromNow;
    event.callback = std::move(callback);
    m_heap.push_back(std::move(event));
    std::push_heap(m_heap.begin(), m_heap.end(), &Scheduler::heapComp);
    return id;
}

void Scheduler::tick(Time cycles)
{
    const Time endTime = m_now + cycles;

    while (!m_heap.empty())
    {
        std::pop_heap(m_heap.begin(), m_heap.end(), &Scheduler::heapComp);
        Event next = std::move(m_heap.back());
        if (next.time > endTime)
        {
            m_heap.back() = std::move(next);
            std::push_heap(m_heap.begin(), m_heap.end(), &Scheduler::heapComp);
            break;
        }
        m_heap.pop_back();
        m_now = next.time;
        if (next.callback)
        {
            next.callback();
        }
    }

    m_now = endTime;
}

void Scheduler::reset()
{
    m_heap.clear();
    m_now = 0;
    m_nextId = 1;
}

} // namespace runtime
} // namespace psxrecomp
