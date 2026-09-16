#pragma once
#include "types.hpp"
#include <functional>
#include <queue>
#include <vector>

namespace gb {

using Cycle = std::int64_t;

class Scheduler {
public:
    using Callback = std::function<void(Cycle firedAt, Cycle lateBy)>;

    void schedule(Cycle when, Callback callback) {
        queue.push(Event{when, nextSequence++, std::move(callback)});
    }

    Cycle now() const { return currentCycle; }

    bool hasEvents() const { return !queue.empty(); }

    Cycle nextEventTime() const {
        return queue.empty() ? currentCycle + kMaxIdleCycles : queue.top().timestamp;
    }

    Cycle cycleBudget() const {
        Cycle delta = nextEventTime() - currentCycle;
        return delta > 0 ? delta : 0;
    }

    void advanceTo(Cycle target) {
        currentCycle = target;
        while (!queue.empty() && queue.top().timestamp <= currentCycle) {
            Event e = queue.top();
            queue.pop();
            e.callback(e.timestamp, currentCycle - e.timestamp);
        }
    }

    void reset() {
        currentCycle = 0;
        nextSequence = 0;
        queue = {}; // what does this do with the old queue?
    }

private:
    struct Event {
        Cycle timestamp;
        std::uint64_t sequence;
        Callback callback;

        bool operator>(const Event& other) const {
            return (timestamp != other.timestamp) ? (timestamp > other.timestamp)
                                                  : (sequence > other.sequence);
        }
    };
    std::priority_queue<Event, std::vector<Event>, std::greater<>> queue;
    static constexpr Cycle kMaxIdleCycles = 65536;
    Cycle currentCycle = 0;
    std::uint64_t nextSequence = 0;
};

} // namespace gb