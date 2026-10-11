#pragma once

// Call-site verification, kept free of CommonLib so it can be unit-tested
// without Skyrim (tests/CallSiteTests.cpp).

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace CallSite
{
    // "xor edx, edx; mov rcx, rbx; call rel32" -- EndInterruptPackage(actor, false).
    inline constexpr std::uint8_t kSetup[] = { 0x33, 0xD2, 0x48, 0x8B, 0xCB, 0xE8 };

    // Bytes of setup before the E8; a site's window starts this far before it.
    inline constexpr std::size_t kLead = sizeof(kSetup) - 1;

    // The window read around a site: the setup, then the call's rel32.
    inline constexpr std::size_t kWindow = kLead + 5;

    enum class Result
    {
        kOk,
        kBytesDiffer,
        kWrongTarget,
    };

    // a_window holds kWindow bytes starting at a_site - kLead; a_site is the
    // address of the E8. kOk only if the setup matches byte for byte and the
    // call resolves to a_target.
    [[nodiscard]] inline Result Verify(const std::uint8_t* a_window, std::uintptr_t a_site, std::uintptr_t a_target) noexcept
    {
        if (std::memcmp(a_window, kSetup, sizeof(kSetup)) != 0) {
            return Result::kBytesDiffer;
        }
        std::int32_t rel = 0;
        std::memcpy(&rel, a_window + sizeof(kSetup), sizeof(rel));
        const auto resolved = a_site + 5 + static_cast<std::intptr_t>(rel);
        return resolved == a_target ? Result::kOk : Result::kWrongTarget;
    }
}
