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
// 1.1.0 (2026-10-07): 41331 calls EndInterruptPackage from a second place,
// +0x36F, with the same "xor edx, edx; mov rcx, rbx; call" -- Kev's game
// froze there with 1.0.0 installed (a debugger snapshot of the frozen
// thread: 41331+0x374 -> 37474 -> 39187 -> 39803 -> 20164 waiting to write
// the player's lock its own thread had read-locked at 41331+0xC8). Both
// call sites are redirected; each is checked byte for byte first.

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

    // One call site: "xor edx, edx; mov rcx, rbx; call 37474", checked byte
    // for byte, then sent to DeferredEndInterruptPackage.
    bool InstallAt(std::uintptr_t a_function, std::uintptr_t a_offset, std::uintptr_t a_target)
    {
        static constexpr std::uint8_t kSetup[] = { 0x33, 0xD2, 0x48, 0x8B, 0xCB, 0xE8 };
        const auto site = a_function + a_offset;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(site - 5);
        if (std::memcmp(bytes, kSetup, sizeof(kSetup)) != 0) {
            SKSE::log::warn("+0x{:X} not installed: the call site doesn't match ({:02X} {:02X} {:02X} {:02X} {:02X} {:02X}); another mod or game version?",
                            a_offset, bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
            return false;
        }
        std::int32_t rel = 0;
        std::memcpy(&rel, reinterpret_cast<const void*>(site + 1), sizeof(rel));
        if (site + 5 + rel != a_target) {
            SKSE::log::warn("+0x{:X} not installed: the call there doesn't go to Actor::EndInterruptPackage", a_offset);
            return false;
        }
        const auto original = reinterpret_cast<EndInterruptPackageFn>(
            SKSE::GetTrampoline().write_call<5>(site, &DeferredEndInterruptPackage, true));
        if (!g_endInterruptPackage) {
            g_endInterruptPackage = original;
        }
        SKSE::log::info("installed at SkyrimSE+{:X} (41331+0x{:X}; EndInterruptPackage deferred out of the player's read lock)",
                        site - REL::Module::get().base(), a_offset);
        return true;
    }

    void Install()
    {
        const auto function = REL::Relocation<std::uintptr_t>{ REL::ID(41331) }.address();
        const auto target = REL::Relocation<std::uintptr_t>{ REL::ID(37474) }.address();
        SKSE::AllocTrampoline(2 * 14);
        for (const std::uintptr_t offset : { 0x171u, 0x36Fu }) {
            InstallAt(function, offset, target);
        }
    }
}

namespace
{
    constexpr SKSE::PluginVersionData MakePluginVersionData()
    {
        SKSE::PluginVersionData version{};
        version.PluginVersion({ 1, 1, 0, 0 });
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
    a_info->version = 2;
    return true;
}

extern "C" __declspec(dllexport) bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
    REL::Module::reset();
    SKSE::Init(a_skse);
    SKSE::log::info("{} 1.1.0 loading", kName);
    Install();
    return true;
}
