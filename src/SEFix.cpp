#include "pch.h"
// Local SE 1.5.97 extension of KezC7's InterruptPackageDeadlockFix approach.
// Adds the Actor::EndDialogue virtual call at 40321+0x34C established by
// the 2026-10-23 NFF horse/Gorr freeze. GPL-3.0-or-later + Modding Exception.
#include "SEFix.h"
#include "DeferredCleanup.h"
#include "PatchPolicy.h"

#include <array>
#include <atomic>
#include <span>

namespace
{
    DeferredCleanup::Session session;
    using EndInterruptFn = void (*)(RE::Actor*, bool);
    EndInterruptFn originalInterrupt = nullptr;
    std::array<std::atomic<std::uint32_t>, 3> queued{}, executed{};
    bool Sample(std::uint32_t n)
    {
        return n <= 20 || n % 100 == 0;
    }
    template <std::size_t Site, class Action> void Queue(RE::Actor* actor, Action action)
    {
        if (!actor)
            return;
        const auto id = actor->GetFormID();
        // BSHandleRefObject reference count, as held by the engine's iterator.
        const bool accepted = DeferredCleanup::Submit(
            RE::NiPointer<RE::Actor>{actor}, session,
            [](auto task) { SKSE::GetTaskInterface()->AddTask(std::move(task)); },
            [action, id](RE::Actor* retained)
            {
                action(retained);
                const auto count = ++executed[Site];
                if (Sample(count))
                    SKSE::log::info("completed {} #{} actor {:08X}", Site == 1 ? "EndDialogue" : "EndInterruptPackage",
                                    count, id);
            });
        if (accepted)
        {
            const auto count = ++queued[Site];
            if (Sample(count))
                SKSE::log::info("deferred 40321+{:X} #{} actor {:08X}", PatchPolicy::Sites[Site], count, id);
        }
    }
    template <std::size_t Site> void EndInterrupt(RE::Actor* actor, bool skipDialogue)
    {
        Queue<Site>(actor, [skipDialogue](RE::Actor* retained) { originalInterrupt(retained, skipDialogue); });
    }
    void EndDialogue(RE::Actor* actor)
    {
        // Preserve virtual dispatch (including any Actor vtable hook). Do not
        // globally hook EndDialogue: only defer the call under this read guard.
        Queue<1>(actor, [](RE::Actor* retained) { retained->EndDialogue(); });
    }
    void OnMessage(SKSE::MessagingInterface::Message* message)
    {
        if (!message)
            return;
        switch (message->type)
        {
        case SKSE::MessagingInterface::kPreLoadGame:
            session.BeforeLoad();
            break;
        case SKSE::MessagingInterface::kPostLoadGame:
            session.AfterLoad();
            break;
        case SKSE::MessagingInterface::kNewGame:
            session.NewGame();
            break;
        default:
            break;
        }
    }
} // namespace

namespace SEFix
{
    bool Install()
    {
        auto* tasks = SKSE::GetTaskInterface();
        auto* messages = SKSE::GetMessagingInterface();
        if (!tasks || !messages)
        {
            SKSE::log::error("not installed: task/messaging interface unavailable; game code unchanged");
            return false;
        }
        const auto base = REL::Module::get().base();
        const auto function = REL::Relocation<std::uintptr_t>{REL::ID(40321)}.address();
        const auto target = REL::Relocation<std::uintptr_t>{REL::ID(36475)}.address();
        const std::span<const std::uint8_t> code{reinterpret_cast<const std::uint8_t*>(function), 0x500};
        if (!PatchPolicy::Verify(code, function - base, target - base))
        {
            SKSE::log::error(
                "not installed: runtime code differs from validated 1.5.97 windows; all three sites unchanged "
                "(check for another cleanup-fix DLL)");
            return false;
        }
        if (!messages->RegisterListener(OnMessage))
        {
            SKSE::log::error("not installed: cannot register save-load guard; game code unchanged");
            return false;
        }
        originalInterrupt = reinterpret_cast<EndInterruptFn>(target);
        SKSE::AllocTrampoline(64);
        auto& trampoline = SKSE::GetTrampoline();
        const std::array<std::uintptr_t, 3> thunks{reinterpret_cast<std::uintptr_t>(&EndInterrupt<0>),
                                                   reinterpret_cast<std::uintptr_t>(&EndDialogue),
                                                   reinterpret_cast<std::uintptr_t>(&EndInterrupt<2>)};
        return PatchPolicy::Install(
            code, function - base, target - base,
            [&](std::size_t i, std::size_t offset, std::size_t width)
            {
                if (width == 6)
                    trampoline.write_call<6>(function + offset, thunks[i]);
                else
                    trampoline.write_call<5>(function + offset, thunks[i]);
                SKSE::log::info("installed 40321+{:X} SkyrimSE+{:X}: {} deferred to main-thread task", offset,
                                function - base + offset, i == 1 ? "EndDialogue" : "EndInterruptPackage");
            });
    }
} // namespace SEFix
