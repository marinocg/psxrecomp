#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace psxrecomp
{
namespace runtime
{

class Scheduler
{
  public:
    using EventId = uint64_t;
    using Callback = std::function<void()>;

    EventId schedule(uint64_t cycles, Callback callback);
    void tick(uint64_t cycles);
    void reset();

  private:
    struct Event
    {
        EventId id;
        uint64_t cyclesRemaining;
        Callback callback;
    };

    std::vector<Event> m_events;
    EventId m_nextId = 1;
};

} // namespace runtime
} // namespace psxrecomp
