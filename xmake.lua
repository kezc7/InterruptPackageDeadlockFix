set_xmakever('3.0.1')
includes('lib/commonlibsse-ng')

set_project('InterruptPackageDeadlockFix')
set_license('MIT')

set_languages('c++23')
set_warnings('allextra')
set_policy('package.requires_lock', true)
set_toolset('msvc', 'ninja')

add_rules('mode.debug', 'mode.releasedbg', 'mode.release')

-- Skyrim AE 1.6.1170. Configure once with:
--   xmake f -P . -y -p windows -a x64 -m release --skyrim_ae=y --skyrim_se=n --skyrim_vr=n
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
