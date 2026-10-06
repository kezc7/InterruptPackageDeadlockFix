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
// The fix: that one call (ID 41331 + 0x171) is sent here instead, and the
// package is ended a moment later on the main thread, after the AI update
// has released the lock. The actor is kept alive with a reference until
// then. Nothing else in the game is changed.

#include "pch.h"

#include <atomic>
#include <cstring>

namespace
{
    constexpr std::string_view kName = "InterruptPackageDeadlockFix";

    using EndInterruptPackageFn = void (*)(RE::Actor*, bool);
    EndInterruptPackageFn g_endInterruptPackage = nullptr;
    std::atomic<std::uint32_t> g_deferred{ 0 };

    void DeferredEndInterruptPackage(RE::Actor* a_actor, bool a_skipDialogue)
    {
        if (!a_actor) {
            return;
        }
        const auto count = ++g_deferred;
        if (count <= 20 || count % 100 == 0) {
            SKSE::log::info("deferred EndInterruptPackage #{} for {:08X} until the AI update lets go of the player's lock",
                            count, a_actor->GetFormID());
        }
        RE::NiPointer<RE::Actor> keep{ a_actor };
        SKSE::GetTaskInterface()->AddTask([keep, a_skipDialogue]() {
            if (keep && !keep->IsDeleted()) {
                g_endInterruptPackage(keep.get(), a_skipDialogue);
            }
        });
    }

    void Install()
    {
        // xor edx, edx; mov rcx, rbx; call Actor::EndInterruptPackage
        static constexpr std::uint8_t kExpected[] = { 0x33, 0xD2, 0x48, 0x8B, 0xCB, 0xE8, 0x8A, 0x81, 0xF0, 0xFF };
        const auto function = REL::Relocation<std::uintptr_t>{ REL::ID(41331) }.address();
        const auto site = function + 0x171;
        const auto target = REL::Relocation<std::uintptr_t>{ REL::ID(37474) }.address();
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(site - 5);
        if (std::memcmp(bytes, kExpected, sizeof(kExpected)) != 0) {
            SKSE::log::warn("not installed: the call site doesn't match ({:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}); another mod or game version?",
                            bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8], bytes[9]);
            return;
        }
        std::int32_t rel = 0;
        std::memcpy(&rel, reinterpret_cast<const void*>(site + 1), sizeof(rel));
        if (site + 5 + rel != target) {
            SKSE::log::warn("not installed: the call at the site doesn't go to Actor::EndInterruptPackage");
            return;
        }
        SKSE::AllocTrampoline(14);
        g_endInterruptPackage = reinterpret_cast<EndInterruptPackageFn>(
            SKSE::GetTrampoline().write_call<5>(site, &DeferredEndInterruptPackage, true));
        SKSE::log::info("installed at SkyrimSE+{:X} (EndInterruptPackage deferred out of the player's read lock)",
                        site - REL::Module::get().base());
    }
}

namespace
{
    constexpr SKSE::PluginVersionData MakePluginVersionData()
    {
        SKSE::PluginVersionData version{};
        version.PluginVersion({ 1, 0, 0, 0 });
        version.PluginName(kName);
        version.AuthorName("KezC7");
        version.UsesAddressLibrary();
        version.UsesNoStructs();
        return version;
    }
}

SKSE_PLUGIN_VERSION = MakePluginVersionData();

SKSE_EXPORT bool SKSEPlugin_Query(SKSE::QueryInterface*, SKSE::PluginInfo* a_info)
{
    a_info->infoVersion = SKSE::PluginInfo::kVersion;
    a_info->name = kName.data();
    a_info->version = 1;
    return true;
}

extern "C" __declspec(dllexport) bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
    REL::Module::reset();
    SKSE::Init(a_skse);
    SKSE::log::info("{} 1.0.0 loading", kName);
    Install();
    return true;
}
