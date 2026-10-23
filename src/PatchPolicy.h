#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

// Skyrim SE 1.5.97 only. Windows are from the read-only live capture of
// 40321 in the diagnosed freeze (docs/SE-DIALOGUE-DEADLOCK.md). Include argument setup and surrounding branches.
namespace PatchPolicy
{
    inline constexpr std::uintptr_t FunctionRva = 0x6D7330;
    inline constexpr std::uintptr_t InterruptRva = 0x5E27F0;
    inline constexpr std::array<std::size_t, 3> Sites{0x172, 0x34C, 0x36A};
    inline constexpr std::array<std::size_t, 3> Widths{5, 6, 5};
    inline constexpr std::uint8_t Entry[]{0x48, 0x8b, 0xc4, 0x44, 0x88, 0x40, 0x18, 0x48, 0x89, 0x48, 0x08, 0x55,
                                          0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d};
    inline constexpr std::uint8_t ReadGuard[]{0x48, 0x8b, 0x0d, 0x0d, 0xfb, 0x84, 0x02, 0x48, 0x83, 0xe9,
                                              0x80, 0x48, 0x89, 0x4d, 0x07, 0xe8, 0xd8, 0xfe, 0x52, 0x00};
    inline constexpr std::uint8_t First[]{0x33, 0xd2, 0x48, 0x8b, 0xcb, 0xe8, 0x49, 0xb3, 0xf0, 0xff, 0x90};
    inline constexpr std::uint8_t Dialogue[]{0x48, 0x8b, 0xcb, 0x84, 0xc0, 0x74, 0x0b, 0x48, 0x8b,
                                             0x03, 0xff, 0x90, 0xd0, 0x06, 0x00, 0x00, 0xeb, 0x33};
    inline constexpr std::uint8_t Second[]{0x33, 0xd2, 0x48, 0x8b, 0xcb, 0xe8, 0x51, 0xb1, 0xf0, 0xff, 0xeb, 0x16};
    inline constexpr std::uint8_t Release[]{0x8b, 0x4d, 0x07, 0x48, 0x85, 0xc9, 0x74, 0x06, 0xe8, 0x74,
                                            0xfd, 0x52, 0x00, 0x90, 0x48, 0x8b, 0x7d, 0x0f, 0x48, 0x85};
    struct Window
    {
        std::size_t offset;
        std::span<const std::uint8_t> bytes;
    };
    inline const std::array<Window, 6> Windows{
        {{0, Entry}, {0xB4, ReadGuard}, {0x16D, First}, {0x342, Dialogue}, {0x365, Second}, {0x4DF, Release}}};
    inline bool Verify(std::span<const std::uint8_t> code, std::uintptr_t functionRva, std::uintptr_t targetRva)
    {
        if (functionRva != FunctionRva || targetRva != InterruptRva)
            return false;
        for (const auto& w : Windows)
        {
            if (w.offset + w.bytes.size() > code.size() ||
                std::memcmp(code.data() + w.offset, w.bytes.data(), w.bytes.size()) != 0)
                return false;
        }
        for (const auto i : {0, 2})
        {
            std::int32_t displacement{};
            std::memcpy(&displacement, code.data() + Sites[i] + 1, 4);
            if (static_cast<std::int64_t>(functionRva + Sites[i] + 5) + displacement !=
                static_cast<std::int64_t>(targetRva))
                return false;
        }
        return true;
    }
    // Validate every affected site before writing any one of them. Never partially
    // fix this function when another DLL/version has changed any expected window.
    template <class Writer>
    bool Install(std::span<const std::uint8_t> code, std::uintptr_t functionRva, std::uintptr_t targetRva,
                 Writer&& write)
    {
        if (!Verify(code, functionRva, targetRva))
            return false;
        for (std::size_t i = 0; i < Sites.size(); ++i)
            write(i, Sites[i], Widths[i]);
        return true;
    }
} // namespace PatchPolicy
