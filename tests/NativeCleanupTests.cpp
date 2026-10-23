#include "pch.h"
// Optional Windows integration tests: exercise actual CommonLib trampolines
// in synthetic executable memory and the built plugin's loader gates.
// No Skyrim process is started and no game code is executed.
#include <Windows.h>
#include <iostream>
unsigned checks = 0;
void Check(bool pass, const char* what)
{
    ++checks;
    if (!pass)
        throw std::runtime_error(what);
}
// Exercise the real CommonLib 5/6-byte trampoline writer on executable test
// memory, including the exact six-byte virtual-call opcode from this freeze.
int intercepted = 0, originalCalls = 0;
void Replacement(void* actor)
{
    if (actor == reinterpret_cast<void*>(0x1234))
        ++intercepted;
}
void Original(void*)
{
    ++originalCalls;
}
void TrampolineTests()
{
    std::cout << "allocate native test code" << std::endl;
    auto* code =
        static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    Check(code != nullptr, "allocate synthetic executable test memory");
    SKSE::Trampoline trampoline;
    // CommonLib anchors its normal allocation at the END of .text, not at
    // the first call. Match that: starting exactly at the first byte could
    // choose a slot at -2GiB, out of rel32 range of a later instruction.
    trampoline.create(64, code + 4096);
    std::cout << "trampoline allocated" << std::endl;
    // sub rsp,28; mov rax,vtable; call [rax+6D0]; add rsp,28; ret
    std::array<std::uintptr_t, 0xDB> vtable{};
    vtable[0xDA] = reinterpret_cast<std::uintptr_t>(&Original);
    const std::uint8_t virtualStub[]{0x48, 0x83, 0xec, 0x28, 0x48, 0xb8, 0, 0,    0,    0,    0,    0,   0,
                                     0,    0xff, 0x90, 0xd0, 6,    0,    0, 0x48, 0x83, 0xc4, 0x28, 0xc3};
    std::memcpy(code, virtualStub, sizeof virtualStub);
    const auto table = reinterpret_cast<std::uintptr_t>(vtable.data());
    std::memcpy(code + 6, &table, 8);
    auto execute = reinterpret_cast<void (*)(void*)>(code);
    execute(reinterpret_cast<void*>(0x1234));
    Check(originalCalls == 1, "unpatched virtual slot calls original");
    std::cout << "original virtual stub passed" << std::endl;
    trampoline.write_call<6>(reinterpret_cast<std::uintptr_t>(code + 14), &Replacement);
    FlushInstructionCache(GetCurrentProcess(), code, 4096);
    execute(reinterpret_cast<void*>(0x1234));
    Check(intercepted == 1 && originalCalls == 1, "six-byte virtual call redirected, argument/return preserved");
    Check(std::memcmp(code + 20, virtualStub + 20, 5) == 0, "virtual-call successor unchanged");
    // sub rsp,28; call local ret; add rsp,28; ret
    const std::uint8_t directStub[]{0x48, 0x83, 0xec, 0x28, 0xe8, 0x37, 0, 0, 0, 0x48, 0x83, 0xc4, 0x28, 0xc3};
    std::memcpy(code + 128, directStub, sizeof directStub);
    code[192] = 0xc3;
    trampoline.write_call<5>(reinterpret_cast<std::uintptr_t>(code + 132), &Replacement);
    FlushInstructionCache(GetCurrentProcess(), code, 4096);
    reinterpret_cast<void (*)(void*)>(code + 128)(reinterpret_cast<void*>(0x1234));
    Check(intercepted == 2, "five-byte call redirected, argument/return preserved");
    Check(std::memcmp(code + 137, directStub + 9, 5) == 0, "direct-call successor unchanged");
    VirtualFree(code, 0, MEM_RELEASE);
}
void LoaderTests(const char* path)
{
    std::cout << "loading built DLL" << std::endl;
    auto library = LoadLibraryA(path);
    Check(library != nullptr, "built DLL loads and dependencies resolve");
    auto query = reinterpret_cast<bool (*)(const SKSE::QueryInterface*, SKSE::PluginInfo*)>(
        GetProcAddress(library, "SKSEPlugin_Query"));
    auto load = reinterpret_cast<bool (*)(const SKSE::LoadInterface*)>(GetProcAddress(library, "SKSEPlugin_Load"));
    Check(query && load, "both SKSE exports present");
    SKSE::detail::SKSEInterface skseInterface{};
    SKSE::PluginInfo info{};
    skseInterface.runtimeVersion = REL::Version{1, 5, 97, 0}.pack();
    Check(query(reinterpret_cast<SKSE::QueryInterface*>(&skseInterface), &info), "SE 1.5.97 accepted");
    Check(std::string(info.name) == "InterruptPackageDeadlockFix", "plugin identity");
    skseInterface.runtimeVersion = REL::Version{1, 6, 1170, 0}.pack();
    Check(query(reinterpret_cast<SKSE::QueryInterface*>(&skseInterface), &info), "existing AE runtime still accepted");
    skseInterface.isEditor = 1;
    Check(!query(reinterpret_cast<SKSE::QueryInterface*>(&skseInterface), &info), "editor rejected");
    skseInterface.isEditor = 0;
    for (const auto& version : {REL::Version{1, 6, 640, 0}, REL::Version{1, 4, 15, 0}, REL::Version{1, 5, 80, 0}})
    {
        skseInterface.runtimeVersion = version.pack();
        Check(!query(reinterpret_cast<SKSE::QueryInterface*>(&skseInterface), &info), "unsupported runtime query rejected");
        Check(!load(reinterpret_cast<SKSE::LoadInterface*>(&skseInterface)),
              "unsupported runtime load rejected before SKSE init");
    }
    Check(!load(nullptr), "null load rejected");
    FreeLibrary(library);
}
int main(int argc, char** argv)
{
    try
    {
        if (argc != 2)
            throw std::runtime_error("expected built DLL path");
        TrampolineTests();
        LoaderTests(argv[1]);
        std::cout << "PASS: " << checks << " native trampoline/loader checks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
