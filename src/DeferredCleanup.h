#pragma once
#include <atomic>
#include <cstdint>
#include <utility>

namespace DeferredCleanup
{
    // No serialized state. A pre-load/new-game invalidates work from the old world.
    struct Session
    {
        std::atomic<std::uint64_t> epoch{0};
        std::atomic<bool> loading{false};
        void BeforeLoad()
        {
            loading.store(true);
            epoch.fetch_add(1);
        }
        void AfterLoad()
        {
            loading.store(false);
        }
        void NewGame()
        {
            epoch.fetch_add(1);
            loading.store(false);
        }
    };
    template <class StrongActor, class Enqueue, class Action>
    bool Submit(StrongActor actor, Session& session, Enqueue&& enqueue, Action action)
    {
        const auto epoch = session.epoch.load();
        // Sample the epoch before loading: if a whole load transition races this
        // check, the queued old-world task will still fail the epoch comparison.
        if (!actor || session.loading.load())
            return false;
        enqueue(
            [actor = std::move(actor), epoch, &session, action = std::move(action)]() mutable
            {
                if (session.loading.load() || session.epoch.load() != epoch || actor->IsDeleted())
                    return;
                action(actor.get());
            });
        return true;
    }
} // namespace DeferredCleanup
