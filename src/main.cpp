// Interrupt Package Deadlock Fix (Skyrim AE 1.6.1170, SKSE).
//
// A vanilla engine deadlock, traced in Kev's game on 2026-10-06 (Wayframe's
// lock tracer, then a debugger snapshot of the frozen process):
//
//   An AI update on a worker thread (Address Library ID 41331) walks a list
//   of actors while holding a READ lock on the player's extra-data lock
//   (player + 0x88). For an actor whose interrupt package targets the object
//   being processed, it calls Actor::EndInterruptPackage (ID 37474) without
//   letting go of that lock. Ending the package can update the player's
//   game-only extra data (IDs 39187 -> 39803 -> 20164, which write-locks the
//   player's extra data and calls AddChange(kGameOnlyExtra)). A write lock
//   waits for every reader to leave -- including this thread's own read --
//   so the thread waits on itself forever, and the main thread waits on it:
//   the game freezes. Seen with the follower Remiel (C1033226) in combat.
//
// The fix: those calls are sent here instead, and the package is ended a
// moment later on the main thread, after the AI update has released the
// lock. The actor is kept alive with a reference until then. Nothing else
// in the game is changed.
//
// Build 2 (2026-10-07): 41331 calls EndInterruptPackage from a second place,
// +0x36F, with the same "xor edx, edx; mov rcx, rbx; call" -- Kev's game
// froze there with build 1 installed (a debugger snapshot of the frozen
// thread: 41331+0x374 -> 37474 -> 39187 -> 39803 -> 20164 waiting to write
// the player's lock its own thread had read-locked at 41331+0xC8). Both
// call sites are redirected; each is checked byte for byte first.
//
// 1.0.0, the first release (2026-10-11): runs only on 1.6.1170; both sites are checked before
// either is patched, and if either doesn't match neither is: deferring
// +0x171 leaves the actor's interrupt package in place for the rest of that
// 41331 call, and the checks after it can then lead to +0x36F, so +0x171
// alone could be worse than vanilla. The log names the call site for each
// deferral. Behaviour of the deferral itself is unchanged.

#include "pch.h"

#include "CallSite.h"

#include <atomic>
#include <format>
#include <iterator>
#include <string>

namespace
{
    constexpr std::string_view kName = "InterruptPackageDeadlockFix";
    constexpr REL::Version     kVersion{ 1, 0, 0, 0 };
    constexpr REL::Version     kRuntime = SKSE::RUNTIME_SSE_1_6_1170;

    using EndInterruptPackageFn = void (*)(RE::Actor*, bool);
    EndInterruptPackageFn g_endInterruptPackage = nullptr;

    // Log the first 20, then every 100th.
    bool ShouldLog(std::uint32_t a_count)
    {
        return a_count <= 20 || a_count % 100 == 0;
    }

    void Defer(RE::Actor* a_actor, bool a_skipDialogue, std::uintptr_t a_offset, std::atomic<std::uint32_t>& a_count)
    {
        if (!a_actor) {
            return;
        }
        if (const auto count = ++a_count; ShouldLog(count)) {
            SKSE::log::info("+0x{:X}: deferred EndInterruptPackage #{} for {:08X} until the AI update lets go of the player's lock",
                            a_offset, count, a_actor->GetFormID());
        }
        // The game's own reference count (BSHandleRefObject), the one 41331
        // holds on this actor while it runs: the actor can't be freed before
        // the task has run.
        RE::NiPointer<RE::Actor> keep{ a_actor };
        SKSE::GetTaskInterface()->AddTask([keep, a_skipDialogue]() {
            if (keep->IsDeleted()) {
                static std::atomic<std::uint32_t> skipped{ 0 };
                if (const auto count = ++skipped; ShouldLog(count)) {
                    SKSE::log::info("skipped deferred EndInterruptPackage #{} for {:08X}: deleted before the main thread got to it",
                                    count, keep->GetFormID());
                }
                return;
            }
            g_endInterruptPackage(keep.get(), a_skipDialogue);
        });
    }

    // One per call site, so the log says which one fired.
    template <std::uintptr_t Offset>
    void DeferredEndInterruptPackage(RE::Actor* a_actor, bool a_skipDialogue)
    {
        static std::atomic<std::uint32_t> count{ 0 };
        Defer(a_actor, a_skipDialogue, Offset, count);
    }

    std::string Hex(const std::uint8_t* a_bytes, std::size_t a_count)
    {
        std::string out;
        for (std::size_t i = 0; i < a_count; ++i) {
            out += std::format("{}{:02X}", i ? " " : "", a_bytes[i]);
        }
        return out;
    }

    struct Site
    {
        std::uintptr_t offset;
        std::uintptr_t thunk;
    };

    constexpr std::uintptr_t kSites[] = { 0x171, 0x36F };

    // Both call sites are checked before either is patched: all or nothing.
    bool Install()
    {
        if (!SKSE::GetTaskInterface()) {
            SKSE::log::error("not installed: SKSE gave no task interface");
            return false;
        }

        const auto function = REL::Relocation<std::uintptr_t>{ REL::ID(41331) }.address();
        const auto target = REL::Relocation<std::uintptr_t>{ REL::ID(37474) }.address();
        const Site sites[] = {
            { kSites[0], reinterpret_cast<std::uintptr_t>(&DeferredEndInterruptPackage<kSites[0]>) },
            { kSites[1], reinterpret_cast<std::uintptr_t>(&DeferredEndInterruptPackage<kSites[1]>) },
        };

        for (const auto& site : sites) {
            const auto  address = function + site.offset;
            const auto* window = reinterpret_cast<const std::uint8_t*>(address - CallSite::kLead);
            switch (CallSite::Verify(window, address, target)) {
            case CallSite::Result::kOk:
                break;
            case CallSite::Result::kBytesDiffer:
                SKSE::log::error("not installed: 41331+0x{:X} doesn't match ({}); another mod may have patched it. Nothing was changed.",
                                 site.offset, Hex(window, CallSite::kWindow));
                return false;
            case CallSite::Result::kWrongTarget:
                SKSE::log::error("not installed: the call at 41331+0x{:X} doesn't go to Actor::EndInterruptPackage; another mod may have hooked it. Nothing was changed.",
                                 site.offset);
                return false;
            }
        }

        // Both verified as direct calls to 37474, so that is what the
        // deferred task calls. Set before patching.
        g_endInterruptPackage = reinterpret_cast<EndInterruptPackageFn>(target);

        SKSE::AllocTrampoline(std::size(sites) * 14);
        auto& trampoline = SKSE::GetTrampoline();
        for (const auto& site : sites) {
            const auto address = function + site.offset;
            trampoline.write_call<5>(address, site.thunk);
            SKSE::log::info("installed at SkyrimSE+{:X} (41331+0x{:X}; EndInterruptPackage deferred out of the player's read lock)",
                            address - REL::Module::get().base(), site.offset);
        }
        return true;
    }
}

namespace
{
    constexpr SKSE::PluginVersionData MakePluginVersionData()
    {
        SKSE::PluginVersionData version{};
        version.PluginVersion(kVersion);
        version.PluginName(kName);
        version.AuthorName("KezC7");
        version.UsesAddressLibrary();
        version.UsesUpdatedStructs();
        return version;
    }
}

SKSE_PLUGIN_VERSION = MakePluginVersionData();

// Only the SE and VR loaders call this (the AE loader reads
// SKSEPlugin_Version), and this build is for AE 1.6.1170 only: decline.
SKSE_EXPORT bool SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
{
    a_info->infoVersion = SKSE::PluginInfo::kVersion;
    a_info->name = kName.data();
    a_info->version = kVersion.pack();
    return !a_skse->IsEditor() && a_skse->RuntimeVersion() == kRuntime;
}

extern "C" __declspec(dllexport) bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
    // UsesAddressLibrary makes SKSE accept any AE runtime; the offsets and
    // bytes here were only checked on 1.6.1170, so refuse the rest -- before
    // SKSE::Init, which would load the Address Library for that runtime.
    // (SKSE's own log records the plugin as not loaded.)
    if (a_skse->RuntimeVersion() != kRuntime) {
        return false;
    }
    SKSE::Init(a_skse);
    SKSE::log::info("{} {} loading on Skyrim {}", kName, kVersion.string("."), REL::Module::get().version().string("."));
    if (!Install()) {
        SKSE::log::error("the game is unchanged; the freeze this fixes can still happen");
    }
    return true;
}
