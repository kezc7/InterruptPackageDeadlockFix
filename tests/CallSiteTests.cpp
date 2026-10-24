// Unit tests for CallSite::Verify -- no Skyrim needed.
//   xmake build -y callsite-tests && xmake run callsite-tests
//
// Fixtures are the two call sites as they are in SkyrimSE.exe 1.6.1170
// (RVAs from Address Library: 41331 = 0x76C2C0, 37474 = 0x6745C0), read
// from the game's code and matching what the plugin logged installing there.

#include "CallSite.h"

#include <cstdio>
#include <cstring>

namespace
{
    int g_failures = 0;

    void Check(bool a_ok, const char* a_what)
    {
        std::printf("%s  %s\n", a_ok ? "pass" : "FAIL", a_what);
        if (!a_ok) {
            ++g_failures;
        }
    }

    constexpr std::uintptr_t kBase = 0x140000000;
    constexpr std::uintptr_t kFunction = kBase + 0x76C2C0;  // 41331
    constexpr std::uintptr_t kTarget = kBase + 0x6745C0;    // 37474

    // 41331+0x16C: xor edx, edx; mov rcx, rbx; call 37474 (E8 at +0x171)
    constexpr std::uint8_t kSite171[] = { 0x33, 0xD2, 0x48, 0x8B, 0xCB, 0xE8, 0x8A, 0x81, 0xF0, 0xFF };
    // 41331+0x36A: the same, E8 at +0x36F
    constexpr std::uint8_t kSite36F[] = { 0x33, 0xD2, 0x48, 0x8B, 0xCB, 0xE8, 0x8C, 0x7F, 0xF0, 0xFF };

    static_assert(sizeof(kSite171) == CallSite::kWindow);
    static_assert(sizeof(kSite36F) == CallSite::kWindow);

    // SE 1.5.97: 40321 = 0x6D7330, 36475 = 0x5E27F0 (read from the running
    // game by tools/probe).
    constexpr std::uintptr_t kFunctionSE = kBase + 0x6D7330;  // 40321
    constexpr std::uintptr_t kTargetSE = kBase + 0x5E27F0;    // 36475
    // 40321+0x16D: xor edx, edx; mov rcx, rbx; call 36475 (E8 at +0x172)
    constexpr std::uint8_t kSite172SE[] = { 0x33, 0xD2, 0x48, 0x8B, 0xCB, 0xE8, 0x49, 0xB3, 0xF0, 0xFF };
    // 40321+0x365: the same, E8 at +0x36A
    constexpr std::uint8_t kSite36ASE[] = { 0x33, 0xD2, 0x48, 0x8B, 0xCB, 0xE8, 0x51, 0xB1, 0xF0, 0xFF };

    static_assert(sizeof(kSite172SE) == CallSite::kWindow);
    static_assert(sizeof(kSite36ASE) == CallSite::kWindow);
}

int main()
{
    using CallSite::Result;
    using CallSite::Verify;

    Check(Verify(kSite171, kFunction + 0x171, kTarget) == Result::kOk, "+0x171 as shipped in 1.6.1170 verifies");
    Check(Verify(kSite36F, kFunction + 0x36F, kTarget) == Result::kOk, "+0x36F as shipped in 1.6.1170 verifies");
    Check(Verify(kSite172SE, kFunctionSE + 0x172, kTargetSE) == Result::kOk, "SE +0x172 as shipped in 1.5.97 verifies");
    Check(Verify(kSite36ASE, kFunctionSE + 0x36A, kTargetSE) == Result::kOk, "SE +0x36A as shipped in 1.5.97 verifies");
    Check(Verify(kSite172SE, kFunctionSE + 0x172, kTarget) == Result::kWrongTarget, "SE bytes against AE's EndInterruptPackage: wrong target");
    Check(Verify(kSite171, kFunctionSE + 0x172, kTargetSE) == Result::kWrongTarget, "AE bytes at the SE site: wrong target");

    // The rel32 is relative to the site: the same bytes at the wrong offset
    // don't resolve to 37474.
    Check(Verify(kSite171, kFunction + 0x36F, kTarget) == Result::kWrongTarget, "+0x171's bytes at +0x36F: wrong target");
    Check(Verify(kSite171, kFunction + 0x171, kTarget + 1) == Result::kWrongTarget, "target off by one: wrong target");

    // Another plugin's write_call at the site: setup intact, call goes to its trampoline.
    std::uint8_t hooked[CallSite::kWindow];
    std::memcpy(hooked, kSite171, sizeof(hooked));
    hooked[6] = 0x10, hooked[7] = 0x20, hooked[8] = 0x30, hooked[9] = 0x00;
    Check(Verify(hooked, kFunction + 0x171, kTarget) == Result::kWrongTarget, "call redirected by another plugin: wrong target");

    // A 14-byte absolute jump (FF 25) over the site: setup gone.
    std::uint8_t jumped[CallSite::kWindow];
    std::memcpy(jumped, kSite171, sizeof(jumped));
    jumped[0] = 0xFF, jumped[1] = 0x25;
    Check(Verify(jumped, kFunction + 0x171, kTarget) == Result::kBytesDiffer, "site overwritten: bytes differ");

    // Each setup byte matters.
    bool everyByte = true;
    for (std::size_t i = 0; i < sizeof(CallSite::kSetup); ++i) {
        std::uint8_t changed[CallSite::kWindow];
        std::memcpy(changed, kSite36F, sizeof(changed));
        changed[i] ^= 0x01;
        everyByte &= Verify(changed, kFunction + 0x36F, kTarget) == Result::kBytesDiffer;
    }
    Check(everyByte, "a change to any setup byte: bytes differ");

    // A forward call (positive rel32) resolves too.
    const std::uint8_t forward[CallSite::kWindow] = { 0x33, 0xD2, 0x48, 0x8B, 0xCB, 0xE8, 0x00, 0x01, 0x00, 0x00 };
    Check(Verify(forward, 0x1000, 0x1000 + 5 + 0x100) == Result::kOk, "forward rel32 resolves");

    std::printf("%s (%d failed)\n", g_failures ? "FAILED" : "all passed", g_failures);
    return g_failures ? 1 : 0;
}
