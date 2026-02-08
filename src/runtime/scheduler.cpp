#include "psxrecomp/runtime/scheduler.h"

#include <algorithm>
#include <utility>

namespace psxrecomp
{
namespace runtime
{

Scheduler::EventId Scheduler::schedule(uint64_t cycles, Callback callback)
{
    EventId id = m_nextId++;
    m_events.push_back({id, cycles, std::move(callback)});
    return id;
}

void Scheduler::tick(uint64_t cycles)
{
    for (auto& event : m_events)
    {
        if (event.cyclesRemaining > cycles)
        {
            event.cyclesRemaining -= cycles;
        }
        else
        {
            event.cyclesRemaining = 0;
        }
    }

    std::vector<Event> readyEvents;
    auto it = std::remove_if(m_events.begin(), m_events.end(),
                             [&readyEvents](Event& event)
                             {
                                 if (event.cyclesRemaining == 0)
                                 {
                                     readyEvents.push_back(std::move(event));
                                     return true;
                                 }
                                 return false;
                             });
    m_events.erase(it, m_events.end());

    for (auto& event : readyEvents)
    {
        if (event.callback)
        {
            event.callback();
        }
    }
}

void Scheduler::reset()
{
    m_events.clear();
    m_nextId = 1;
}

} // namespace runtime
} // namespace psxrecomp
