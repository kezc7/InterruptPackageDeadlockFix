// Interrupt Package Probe: a log-only SKSE plugin for porting
// InterruptPackageDeadlockFix to Skyrim SE 1.5.97. It patches nothing.
//
// It finds every direct call to Actor::EndInterruptPackage (SE 36475, AE
// 37474) in the running game's code, works out the function each call is in
// (from the exception table, following chained entries) and its Address
// Library ID, and notes which of those functions load the player singleton
// then read-lock the player's extra data (player + 0x80 on SE, + 0x88 on
// AE: BaseExtraList grew by 8 in 1.6.629), as AE 41331 does at +0xB1/+0xC3
// before its calls at +0x171 and +0x36F. For each function that touches the
// player it logs a timeline of lock calls and EndInterruptPackage calls, so
// it shows which calls are made while the read lock is held.
//
// On AE 1.6.1170 the answer is known, so the log ends with a self-check:
// 41331, calls at +0x171 and +0x36F (and whether there are any others). Run that check with
// InterruptPackageDeadlockFix disabled (it rewrites those two calls).
//
// Log: Documents\My Games\Skyrim Special Edition\SKSE\InterruptPackageProbe.log

#include <RE/Skyrim.h>
#include <REL/Relocation.h>
#include <SKSE/SKSE.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <format>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr std::string_view kName = "InterruptPackageProbe";
    constexpr REL::Version     kVersion{ 0, 5, 0, 0 };
    constexpr REL::Version     kSE = SKSE::RUNTIME_SSE_1_5_97;
    constexpr REL::Version     kAE = SKSE::RUNTIME_SSE_1_6_1170;

    template <class T>
    T Read(std::uintptr_t a_address)
    {
        T value;
        std::memcpy(&value, reinterpret_cast<const void*>(a_address), sizeof(T));
        return value;
    }

    std::string Hex(std::uintptr_t a_address, std::size_t a_count)
    {
        std::string out;
        for (std::size_t i = 0; i < a_count; ++i) {
            out += std::format("{}{:02X}", i ? " " : "", Read<std::uint8_t>(a_address + i));
        }
        return out;
    }

    // Logs [a_begin, a_end) as hex, 32 bytes a line, offsets relative to a_begin.
    void Dump(std::string_view a_what, std::uintptr_t a_base, std::uint32_t a_begin, std::uint32_t a_end)
    {
        SKSE::log::info("dump {} SkyrimSE+{:X}..{:X} ({:X} bytes)", a_what, a_begin, a_end, a_end - a_begin);
        for (auto at = a_begin; at < a_end; at += 32) {
            SKSE::log::info("  {:04X}: {}", at - a_begin, Hex(a_base + at, std::min<std::uint32_t>(32, a_end - at)));
        }
    }

    struct RuntimeFunction
    {
        std::uint32_t begin;
        std::uint32_t end;
        std::uint32_t unwind;
    };

    // The module's exception table (.pdata), sorted by begin RVA.
    std::vector<RuntimeFunction> ExceptionTable(std::uintptr_t a_base)
    {
        const auto nt = a_base + Read<std::uint32_t>(a_base + 0x3C);
        const auto directory = nt + 24 + 112 + 3 * 8;  // PE32+ optional header, entry 3
        const auto rva = Read<std::uint32_t>(directory);
        const auto size = Read<std::uint32_t>(directory + 4);
        std::vector<RuntimeFunction> table(size / sizeof(RuntimeFunction));
        std::memcpy(table.data(), reinterpret_cast<const void*>(a_base + rva), table.size() * sizeof(RuntimeFunction));
        return table;
    }

    const RuntimeFunction* Lookup(const std::vector<RuntimeFunction>& a_table, std::uint32_t a_rva)
    {
        auto it = std::upper_bound(a_table.begin(), a_table.end(), a_rva,
                                   [](std::uint32_t a_value, const RuntimeFunction& a_entry) { return a_value < a_entry.begin; });
        if (it == a_table.begin()) {
            return nullptr;
        }
        --it;
        return a_rva < it->end ? &*it : nullptr;
    }

    // Follows UNW_FLAG_CHAININFO back to the function's primary entry.
    RuntimeFunction Primary(std::uintptr_t a_base, RuntimeFunction a_entry)
    {
        for (int guard = 0; guard < 32; ++guard) {
            const auto info = a_base + (a_entry.unwind & ~1u);
            const auto flags = Read<std::uint8_t>(info) >> 3;
            if ((flags & 0x4) == 0) {
                break;
            }
            const auto codes = Read<std::uint8_t>(info + 2);
            a_entry = Read<RuntimeFunction>(info + 4 + ((codes + 1u) & ~1u) * 2);
        }
        return a_entry;
    }

    // Address Library ID of an exact offset, or 0 (Offset2ID's own lookup aborts on a miss).
    std::uint64_t IdOf(const REL::Offset2ID& a_ids, std::size_t a_offset)
    {
        const auto it = std::lower_bound(a_ids.begin(), a_ids.end(), a_offset,
                                         [](const auto& a_elem, std::size_t a_value) { return a_elem.offset < a_value; });
        return it != a_ids.end() && it->offset == a_offset ? it->id : 0;
    }

    struct Function
    {
        RuntimeFunction           primary{};
        std::vector<std::uint32_t> calls;      // RVAs of E8s to EndInterruptPackage
        std::vector<RuntimeFunction> pieces;   // the entries those calls are in
    };

    enum class Event
    {
        kPlayer,      // RIP-relative mov of the player singleton
        kLockOffset,  // add r64, <lock offset> / sub r64, -0x80 / lea r64, [r64 + <lock offset>]
        kLockRead,
        kUnlockRead,
        kLockWrite,
        kUnlockWrite,
        kEnd,  // call to EndInterruptPackage
    };

    constexpr std::string_view Name(Event a_event)
    {
        switch (a_event) {
        case Event::kPlayer:
            return "player";
        case Event::kLockOffset:
            return "+lock offset";
        case Event::kLockRead:
            return "LockForRead";
        case Event::kUnlockRead:
            return "UnlockForRead";
        case Event::kLockWrite:
            return "LockForWrite";
        case Event::kUnlockWrite:
            return "UnlockForWrite";
        case Event::kEnd:
            return "EndInterruptPackage";
        }
        return "?";
    }

    struct Targets
    {
        std::uintptr_t player;
        std::uint32_t  lockOffset;
        std::uintptr_t end;
        std::uintptr_t lockRead;
        std::uintptr_t unlockRead;
        std::uintptr_t lockWrite;
        std::uintptr_t unlockWrite;
    };

    // Events in [a_begin, a_end), by address.
    std::vector<std::pair<std::uintptr_t, Event>> Timeline(std::uintptr_t a_begin, std::uintptr_t a_end, const Targets& a_t)
    {
        std::vector<std::pair<std::uintptr_t, Event>> events;
        for (auto p = a_begin; p + 7 <= a_end; ++p) {
            const auto b0 = Read<std::uint8_t>(p);
            if (b0 == 0xE8) {
                const auto to = p + 5 + static_cast<std::intptr_t>(Read<std::int32_t>(p + 1));
                if (to == a_t.end) {
                    events.emplace_back(p, Event::kEnd);
                } else if (to == a_t.lockRead) {
                    events.emplace_back(p, Event::kLockRead);
                } else if (to == a_t.unlockRead) {
                    events.emplace_back(p, Event::kUnlockRead);
                } else if (to == a_t.lockWrite) {
                    events.emplace_back(p, Event::kLockWrite);
                } else if (to == a_t.unlockWrite) {
                    events.emplace_back(p, Event::kUnlockWrite);
                }
                continue;
            }
            const auto op = Read<std::uint8_t>(p + 1);
            const auto modrm = Read<std::uint8_t>(p + 2);
            if ((b0 & 0xF8) == 0x48 && op == 0x8B && (modrm & 0xC7) == 0x05) {
                if (p + 7 + static_cast<std::intptr_t>(Read<std::int32_t>(p + 3)) == a_t.player) {
                    events.emplace_back(p, Event::kPlayer);
                }
            } else if ((b0 & 0xFA) == 0x48 && op == 0x81 && (modrm & 0xF8) == 0xC0 && Read<std::uint32_t>(p + 3) == a_t.lockOffset) {
                events.emplace_back(p, Event::kLockOffset);
            } else if ((b0 & 0xFA) == 0x48 && op == 0x83 && (modrm & 0xF8) == 0xE8 && a_t.lockOffset == 0x80 && Read<std::uint8_t>(p + 3) == 0x80) {
                events.emplace_back(p, Event::kLockOffset);
            } else if ((b0 & 0xFA) == 0x48 && op == 0x8D && (modrm & 0xC0) == 0x80 && (modrm & 0x07) != 0x04 && Read<std::uint32_t>(p + 3) == a_t.lockOffset) {
                events.emplace_back(p, Event::kLockOffset);
            }
        }
        return events;
    }

    void Probe(bool a_ae)
    {
        const auto& module = REL::Module::get();
        const auto  base = module.base();
        const auto  text = module.segment(REL::Segment::textx);
        const auto  target = REL::Relocation<std::uintptr_t>{ REL::RelocationID(36475, 37474) }.address();
        const auto  player = REL::Relocation<std::uintptr_t>{ REL::RelocationID(517014, 403521) }.address();
        const auto  at = [](std::uint64_t a_se, std::uint64_t a_ae) { return REL::Relocation<std::uintptr_t>{ REL::RelocationID(a_se, a_ae) }.address(); };
        const Targets targets{
            player, a_ae ? 0x88u : 0x80u, target,
            at(66976, 68233), at(66982, 68239), at(66977, 68234), at(66983, 68240)
        };
        const auto  table = ExceptionTable(base);
        const REL::Offset2ID ids;

        SKSE::log::info("EndInterruptPackage at SkyrimSE+{:X} (ID {}), player singleton at SkyrimSE+{:X}; .text SkyrimSE+{:X}, {:X} bytes; {} exception entries",
                        target - base, IdOf(ids, target - base), player - base, text.offset(), text.size(), table.size());

        std::map<std::uint32_t, Function> functions;
        std::size_t                       jumps = 0;
        const auto                        begin = text.address();
        const auto                        end = begin + text.size();
        for (auto p = begin; p + 5 <= end; ++p) {
            const auto op = Read<std::uint8_t>(p);
            if (op != 0xE8 && op != 0xE9) {
                continue;
            }
            if (p + 5 + static_cast<std::intptr_t>(Read<std::int32_t>(p + 1)) != target) {
                continue;
            }
            const auto rva = static_cast<std::uint32_t>(p - base);
            if (op == 0xE9) {
                ++jumps;
                SKSE::log::info("tail jump to it at SkyrimSE+{:X}", rva);
                continue;
            }
            const auto* piece = Lookup(table, rva);
            if (!piece) {
                SKSE::log::info("call at SkyrimSE+{:X} is outside every exception entry", rva);
                continue;
            }
            const auto primary = Primary(base, *piece);
            auto&      function = functions[primary.begin];
            function.primary = primary;
            function.calls.push_back(rva);
            function.pieces.push_back(*piece);
        }

        // A candidate: the player singleton is loaded, the lock offset added,
        // and LockForRead called, all within 0x20 bytes; then at least one
        // EndInterruptPackage call after that LockForRead.
        std::vector<std::pair<std::uint64_t, std::vector<std::uint32_t>>> candidates;
        std::size_t                                                       total = 0;
        for (const auto& [start, function] : functions) {
            total += function.calls.size();
            const auto fid = IdOf(ids, start);

            std::vector<RuntimeFunction> pieces{ function.primary };
            for (const auto& piece : function.pieces) {
                if (std::none_of(pieces.begin(), pieces.end(), [&](const RuntimeFunction& a_p) { return a_p.begin == piece.begin; })) {
                    pieces.push_back(piece);
                }
            }
            std::vector<std::pair<std::uintptr_t, Event>> events;
            for (const auto& piece : pieces) {
                const auto more = Timeline(base + piece.begin, base + piece.end, targets);
                events.insert(events.end(), more.begin(), more.end());
            }
            std::sort(events.begin(), events.end());
            const bool touchesPlayer = std::any_of(events.begin(), events.end(), [](const auto& a_e) { return a_e.second == Event::kPlayer; });

            SKSE::log::info("function SkyrimSE+{:X} (ID {}, {:X} bytes): {} call(s){}",
                            start, fid ? std::to_string(fid) : "none", function.primary.end - function.primary.begin, function.calls.size(),
                            touchesPlayer ? ", touches the player" : "");
            for (std::size_t i = 0; i < function.calls.size(); ++i) {
                const auto rva = function.calls[i];
                const auto inPrimary = function.pieces[i].begin == start;
                SKSE::log::info("    call at +0x{:X}{}: {} | {} | {}",
                                rva - start, inPrimary ? "" : std::format(" (in a separate piece at SkyrimSE+{:X})", function.pieces[i].begin),
                                Hex(base + rva - 8, 8), Hex(base + rva, 5), Hex(base + rva + 5, 8));
            }
            if (!touchesPlayer) {
                continue;
            }

            std::string    line;
            std::uintptr_t lastPlayer = 0;
            std::uintptr_t lastOffset = 0;
            std::uintptr_t playerLock = 0;  // first LockForRead right after player + lock offset
            for (const auto& [where, event] : events) {
                const auto rel = static_cast<std::intptr_t>(where) - static_cast<std::intptr_t>(base + start);
                line += std::format("{}{}0x{:X} {}", line.empty() ? "" : ", ", rel < 0 ? "-" : "+", rel < 0 ? -rel : rel, Name(event));
                if (event == Event::kPlayer) {
                    lastPlayer = where;
                } else if (event == Event::kLockOffset) {
                    lastOffset = where;
                } else if (event == Event::kLockRead && !playerLock && lastPlayer && lastOffset >= lastPlayer &&
                           where - lastPlayer <= 0x20) {
                    playerLock = where;
                }
            }
            SKSE::log::info("    timeline: {}", line);
            for (const auto& [where, event] : events) {
                if (event == Event::kLockRead || event == Event::kUnlockRead || event == Event::kLockWrite || event == Event::kUnlockWrite) {
                    SKSE::log::info("    {} at +0x{:X}, the 0x30 bytes before: {}",
                                    Name(event), where - (base + start), Hex(where - 0x30, 0x30));
                }
            }
            if (playerLock) {
                std::vector<std::uint32_t> after;
                for (const auto rva : function.calls) {
                    if (base + rva > playerLock) {
                        after.push_back(rva - start);
                    }
                }
                SKSE::log::info("    read-locks the player's extra data at +0x{:X}", playerLock - (base + start));
                if (!after.empty()) {
                    candidates.emplace_back(fid, std::move(after));
                }
            }
        }

        for (const auto& [cid, offsets] : candidates) {
            for (const auto& [start, function] : functions) {
                if (IdOf(ids, start) != cid) {
                    continue;
                }
                Dump(std::format("ID {}", cid), base, function.primary.begin, function.primary.end);
                for (const auto& piece : function.pieces) {
                    if (piece.begin != function.primary.begin) {
                        Dump(std::format("ID {} piece", cid), base, piece.begin, piece.end);
                    }
                }
            }
        }
        if (const auto* end = Lookup(table, static_cast<std::uint32_t>(target - base))) {
            const auto primary = Primary(base, *end);
            Dump("EndInterruptPackage", base, primary.begin, primary.end);
        }
        // AE: the chain from the traced freeze, EndInterruptPackage -> 39187 -> 39803 -> 20164,
        // to see whose extra-data lock 20164 write-locks.
        if (a_ae) {
            for (const std::uint64_t chain : { 39187u, 39803u, 20164u }) {
                const auto address = REL::Relocation<std::uintptr_t>{ REL::ID(chain) }.address();
                if (const auto* entry = Lookup(table, static_cast<std::uint32_t>(address - base))) {
                    const auto primary = Primary(base, *entry);
                    Dump(std::format("ID {}", chain), base, primary.begin, primary.end);
                }
            }
        }

        SKSE::log::info("{} direct calls in {} functions, {} tail jumps; {} candidate function(s)",
                        total, functions.size(), jumps, candidates.size());
        for (const auto& [cid, offsets] : candidates) {
            std::string list;
            for (const auto offset : offsets) {
                list += std::format("{}+0x{:X}", list.empty() ? "" : ", ", offset);
            }
            SKSE::log::info("candidate: ID {} read-locks the player, then calls EndInterruptPackage at {}", cid, list);
        }

        if (a_ae) {
            const auto it = std::find_if(candidates.begin(), candidates.end(), [](const auto& a_candidate) { return a_candidate.first == 41331; });
            const bool ok = it != candidates.end() &&
                            std::ranges::find(it->second, 0x171u) != it->second.end() &&
                            std::ranges::find(it->second, 0x36Fu) != it->second.end();
            SKSE::log::info("AE self-check: {}{}",
                            ok ? "PASS (41331 read-locks the player, then calls at +0x171 and +0x36F)" :
                                 "FAIL (41331 at +0x171/+0x36F not found; is InterruptPackageDeadlockFix enabled? disable it for this check)",
                            ok && it->second.size() > 2 ? std::format("; NOTE: {} more call(s) after the lock", it->second.size() - 2) : std::string{});
        }
        SKSE::log::info("done; nothing was patched");
    }
}

SKSE_PLUGIN_VERSION = []() {
    SKSE::PluginVersionData version{};
    version.PluginVersion(kVersion);
    version.PluginName(kName);
    version.AuthorName("KezC7");
    version.UsesAddressLibrary();
    version.UsesUpdatedStructs();
    return version;
}();

// The SE loader (1.5.97) asks here; the AE loader reads SKSEPlugin_Version.
SKSE_EXPORT bool SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
{
    a_info->infoVersion = SKSE::PluginInfo::kVersion;
    a_info->name = kName.data();
    a_info->version = kVersion.pack();
    return !a_skse->IsEditor() && a_skse->RuntimeVersion() == kSE;
}

extern "C" __declspec(dllexport) bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
    const auto runtime = a_skse->RuntimeVersion();
    if (runtime != kSE && runtime != kAE) {
        return false;
    }
    SKSE::Init(a_skse);
    SKSE::log::info("{} {} on Skyrim {} (log only; patches nothing)", kName, kVersion.string("."), runtime.string("."));
    Probe(runtime == kAE);
    return true;
}
