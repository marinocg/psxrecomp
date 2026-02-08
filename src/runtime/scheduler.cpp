#include "psxrecomp/runtime/scheduler.h"

#include <algorithm>

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

    auto it = std::remove_if(m_events.begin(), m_events.end(), [](const Event& event) {
        if (event.cyclesRemaining == 0)
        {
            if (event.callback)
            {
                event.callback();
            }
            return true;
        }
        return false;
    });
    m_events.erase(it, m_events.end());
}

void Scheduler::reset()
{
    m_events.clear();
    m_nextId = 1;
}

} // namespace runtime
} // namespace psxrecomp
