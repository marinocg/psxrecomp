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
    using Time = uint64_t;
    using Callback = std::function<void()>;

    EventId schedule(Time cyclesFromNow, Callback callback);
    void tick(Time cycles);
    void reset();
    Time now() const
    {
        return m_now;
    }

  private:
    struct Event
    {
        EventId id = 0;
        Time time = 0;
        Callback callback;
    };

    static bool heapComp(const Event& a, const Event& b)
    {
        return a.time > b.time;
    }

    std::vector<Event> m_heap;
    Time m_now = 0;
    EventId m_nextId = 1;
};

} // namespace runtime
} // namespace psxrecomp
