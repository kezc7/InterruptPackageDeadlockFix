// Standalone tests: no Skyrim, Address Library, or CommonLib runtime needed.
#include "DeferredCleanup.h"
#include "PatchPolicy.h"
#include "SECaptureFixture.h"
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>
unsigned checks = 0;
void Check(bool pass, const char* what)
{
    ++checks;
    if (!pass)
        throw std::runtime_error(what);
}
struct Actor
{
    bool deleted = false;
    int ended = 0;
    bool skip = false;
    bool IsDeleted() const
    {
        return deleted;
    }
};
void PolicyTests(const std::vector<std::uint8_t>& original)
{
    Check(PatchPolicy::Verify(original, 0x6D7330, 0x5E27F0), "captured live instructions must match");
    std::vector<std::size_t> writes;
    auto write = [&](std::size_t, std::size_t offset, std::size_t width)
    {
        writes.push_back(offset);
        Check(width == (offset == 0x34c ? 6 : 5), "instruction length");
    };
    Check(PatchPolicy::Install(original, 0x6D7330, 0x5E27F0, write), "matching snapshot installs");
    Check(writes == std::vector<std::size_t>{0x172, 0x34c, 0x36a}, "all three exact sites");
    for (const auto& window : PatchPolicy::Windows)
        for (std::size_t i = 0; i < window.bytes.size(); ++i)
        {
            auto changed = original;
            changed[window.offset + i] ^= 0x80;
            writes.clear();
            Check(!PatchPolicy::Install(changed, 0x6D7330, 0x5E27F0, write),
                  "reject changed instructions/another mod hook");
            Check(writes.empty(), "mismatch must write NOTHING, even at earlier matching sites");
        }
    writes.clear();
    Check(!PatchPolicy::Install(original, 0x6D7331, 0x5E27F0, write), "wrong function relocation");
    Check(!PatchPolicy::Install(original, 0x6D7330, 0x5E27F1, write), "wrong callee relocation");
    Check(!PatchPolicy::Install(std::span(original).first(0x172), 0x6D7330, 0x5E27F0, write), "truncated memory view");
    Check(writes.empty(), "relocation/truncation rejects before any write");
}
void QueueTests()
{
    DeferredCleanup::Session session;
    std::vector<std::function<void()>> tasks;
    auto queue = [&](auto task) { tasks.emplace_back(std::move(task)); };
    auto drain = [&]()
    {
        auto batch = std::move(tasks);
        tasks.clear();
        for (auto& task : batch)
            task();
    };
    auto actor = std::make_shared<Actor>();
    std::weak_ptr<Actor> weak = actor;
    std::shared_mutex playerLock;
    bool called = false;
    std::thread worker(
        [&]()
        {
            std::shared_lock read(playerLock);
            DeferredCleanup::Submit(actor, session, queue,
                                    [&](Actor* retained)
                                    {
                                        std::unique_lock write(playerLock);
                                        ++retained->ended;
                                        called = true;
                                    });
            Check(!called, "cleanup must not execute synchronously while read guard held");
        });
    worker.join();
    actor.reset();
    Check(!weak.expired(), "queued work retains actor lifetime");
    drain();
    Check(called, "cleanup completes after read guard released");
    Check(weak.expired(), "actor reference released after execution");
    actor = std::make_shared<Actor>();
    DeferredCleanup::Submit(actor, session, queue, [](Actor* a) { ++a->ended; });
    actor->deleted = true;
    drain();
    Check(actor->ended == 0, "deleted actor skipped");
    actor->deleted = false;
    DeferredCleanup::Submit(actor, session, queue, [](Actor* a) { ++a->ended; });
    session.BeforeLoad();
    session.AfterLoad();
    drain();
    Check(actor->ended == 0, "old-world work skipped after save load");
    DeferredCleanup::Submit(actor, session, queue, [](Actor* a) { ++a->ended; });
    session.NewGame();
    drain();
    Check(actor->ended == 0, "old-world work skipped after new game");
    session.BeforeLoad();
    Check(!DeferredCleanup::Submit(actor, session, queue, [](Actor* a) { ++a->ended; }), "no work queued during load");
    session.AfterLoad();
    Check(!DeferredCleanup::Submit(std::shared_ptr<Actor>{}, session, queue, [](Actor* a) { ++a->ended; }),
          "null actor ignored");
    for (bool skip : {false, true})
    {
        DeferredCleanup::Submit(actor, session, queue,
                                [skip](Actor* a)
                                {
                                    a->skip = skip;
                                    ++a->ended;
                                });
        drain();
        Check(actor->skip == skip, "preserves interrupt-package argument");
    }
    Check(actor->ended == 2, "new-world tasks execute normally");
}
int main()
{
    try
    {
        const auto fixture = SECaptureFixture();
        PolicyTests({fixture.begin(), fixture.end()});
        QueueTests();
        std::cout << "PASS: " << checks << " SE instruction/deferral checks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
