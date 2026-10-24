// Interrupt Package Deadlock Fix (Skyrim AE 1.6.1170 and SE 1.5.97, SKSE).
//
// A vanilla engine deadlock, traced in the author's game on 2026-10-06 (Wayframe's
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
// +0x36F, with the same "xor edx, edx; mov rcx, rbx; call" -- the author's game
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
//
// 1.1.0 (2026-10-17): SE 1.5.97 as well, from the same DLL. 41331 is 40321
// there, with the same code: the player's read lock at +0xC3, then the two
// calls at +0x172 and +0x36A ("xor edx, edx; mov rcx, rbx; call 36475"),
// found in the running SE game with tools/probe and checked against AE's.
// The function's third call (AE +0x7FE, SE +0x848) is left alone: it runs
// in a second loop, over ProcessLists::tempShouldMoves, holding the read
// lock of that actor's own extra data, not the player's; and 20164 write-
// locks the reference whose handle the actor's extra data holds (the player,
// for a follower), so it can only wait on itself if that is the actor
// itself. See docs/TECHNICAL.md.

#include "pch.h"

#include "CallSite.h"

#include <atomic>
#include <format>
#include <iterator>
#include <string>

namespace
{
    constexpr std::string_view kName = "InterruptPackageDeadlockFix";
    constexpr REL::Version     kVersion{ 1, 1, 0, 0 };
    constexpr REL::Version     kRuntimeAE = SKSE::RUNTIME_SSE_1_6_1170;
    constexpr REL::Version     kRuntimeSE = SKSE::RUNTIME_SSE_1_5_97;

    // The AI pass and its two calls made under the player's read lock.
    struct Layout
    {
        std::uint64_t  function;    // Address Library ID
        std::uintptr_t offsets[2];  // of the two E8s
    };
    constexpr Layout kLayoutAE{ 41331, { 0x171, 0x36F } };
    constexpr Layout kLayoutSE{ 40321, { 0x172, 0x36A } };
    const Layout*    g_layout = nullptr;

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
            SKSE::log::info("{}+0x{:X}: deferred EndInterruptPackage #{} for {:08X} until the AI update lets go of the player's lock",
                            g_layout->function, a_offset, count, a_actor->GetFormID());
        }
        // The game's own reference count (BSHandleRefObject), the one the AI
        // pass holds on this actor while it runs: the actor can't be freed before
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
    template <std::size_t Index>
    void DeferredEndInterruptPackage(RE::Actor* a_actor, bool a_skipDialogue)
    {
        static std::atomic<std::uint32_t> count{ 0 };
        Defer(a_actor, a_skipDialogue, g_layout->offsets[Index], count);
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

    // Both call sites are checked before either is patched: all or nothing.
    bool Install(const Layout& a_layout)
    {
        if (!SKSE::GetTaskInterface()) {
            SKSE::log::error("not installed: SKSE gave no task interface");
            return false;
        }

        g_layout = &a_layout;
        const auto id = a_layout.function;
        const auto function = REL::Relocation<std::uintptr_t>{ REL::ID(id) }.address();
        const auto target = REL::Relocation<std::uintptr_t>{ REL::RelocationID(36475, 37474) }.address();
        const Site sites[] = {
            { a_layout.offsets[0], reinterpret_cast<std::uintptr_t>(&DeferredEndInterruptPackage<0>) },
            { a_layout.offsets[1], reinterpret_cast<std::uintptr_t>(&DeferredEndInterruptPackage<1>) },
        };

        for (const auto& site : sites) {
            const auto  address = function + site.offset;
            const auto* window = reinterpret_cast<const std::uint8_t*>(address - CallSite::kLead);
            switch (CallSite::Verify(window, address, target)) {
            case CallSite::Result::kOk:
                break;
            case CallSite::Result::kBytesDiffer:
                SKSE::log::error("not installed: {}+0x{:X} doesn't match ({}); another mod may have patched it. Nothing was changed.",
                                 id, site.offset, Hex(window, CallSite::kWindow));
                return false;
            case CallSite::Result::kWrongTarget:
                SKSE::log::error("not installed: the call at {}+0x{:X} doesn't go to Actor::EndInterruptPackage; another mod may have hooked it. Nothing was changed.",
                                 id, site.offset);
                return false;
            }
        }

        // Both verified as direct calls to EndInterruptPackage, so that is
        // what the deferred task calls. Set before patching.
        g_endInterruptPackage = reinterpret_cast<EndInterruptPackageFn>(target);

        SKSE::AllocTrampoline(std::size(sites) * 14);
        auto& trampoline = SKSE::GetTrampoline();
        for (const auto& site : sites) {
            const auto address = function + site.offset;
            trampoline.write_call<5>(address, site.thunk);
            SKSE::log::info("installed at SkyrimSE+{:X} ({}+0x{:X}; EndInterruptPackage deferred out of the player's read lock)",
                            address - REL::Module::get().base(), id, site.offset);
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
// SKSEPlugin_Version): accept SE 1.5.97, decline everything else.
SKSE_EXPORT bool SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
{
    a_info->infoVersion = SKSE::PluginInfo::kVersion;
    a_info->name = kName.data();
    a_info->version = kVersion.pack();
    return !a_skse->IsEditor() && a_skse->RuntimeVersion() == kRuntimeSE;
}

extern "C" __declspec(dllexport) bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
    // UsesAddressLibrary makes SKSE accept any AE runtime; the offsets and
    // bytes here were only checked on 1.6.1170 and 1.5.97, so refuse the
    // rest -- before SKSE::Init, which would load the Address Library for
    // that runtime. (SKSE's own log records the plugin as not loaded.)
    const auto  runtime = a_skse->RuntimeVersion();
    const auto* layout = runtime == kRuntimeAE ? &kLayoutAE :
                         runtime == kRuntimeSE ? &kLayoutSE :
                                                 nullptr;
    if (!layout) {
        return false;
    }
    SKSE::Init(a_skse);
    SKSE::log::info("{} {} loading on Skyrim {}", kName, kVersion.string("."), REL::Module::get().version().string("."));
    if (!Install(*layout)) {
        SKSE::log::error("the game is unchanged; the freeze this fixes can still happen");
    }
    return true;
}
