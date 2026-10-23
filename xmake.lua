set_xmakever('3.0.1')
includes('lib/commonlibsse-ng')

set_project('InterruptPackageDeadlockFix')
set_version('1.0.1')
set_license('GPL-3.0-or-later')

set_languages('c++23')
set_warnings('allextra')
set_policy('package.requires_lock', true)
set_toolset('msvc', 'ninja')

add_rules('mode.debug', 'mode.releasedbg', 'mode.release')

-- Skyrim AE 1.6.1170 and experimental SE 1.5.97. Configure once with:
--   xmake f -P . -y -p windows -a x64 -m release --skyrim_ae=y --skyrim_se=y --skyrim_vr=n
target('InterruptPackageDeadlockFix')
    add_deps('commonlibsse-ng')
    add_rules('commonlib.plugin')
    on_load(function(target)
        target:data_set('commonlib.plugin.config', {
            name        = 'InterruptPackageDeadlockFix',
            author      = 'KezC7',
            description = "Defers Actor::EndInterruptPackage out of the player extra-data read lock."
        })
    end)
    add_files('src/**.cpp')
    add_includedirs('src')
    set_pcxxheader('src/pch.h')

-- Unit tests for the call-site check (no Skyrim needed):
--   xmake build -y callsite-tests && xmake run callsite-tests
target('callsite-tests')
    set_kind('binary')
    set_default(false)
    add_files('tests/CallSiteTests.cpp')
    add_includedirs('src')

-- Sanitized captured SE instruction windows and deferred-work lifetime tests.
target('se-cleanup-tests')
    set_kind('binary')
    set_default(false)
    add_files('tests/SECleanupTests.cpp')
    add_includedirs('src', 'tests')

-- Windows-only native call redirection / built-DLL loader tests.
-- xmake run native-cleanup-tests <absolute path to built plugin DLL>
target('native-cleanup-tests')
    set_kind('binary')
    set_default(false)
    add_deps('commonlibsse-ng')
    add_files('tests/NativeCleanupTests.cpp')
    add_includedirs('src')
