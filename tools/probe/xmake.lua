-- Interrupt Package Probe: log-only plugin for porting the fix to SE 1.5.97.
-- Its own project (and config), so the release plugin's AE-only build is untouched:
--   xmake f -P tools/probe -y -p windows -a x64 -m release --skyrim_se=y --skyrim_ae=y --skyrim_vr=n
--   xmake build -P tools/probe -y
set_xmakever('3.0.1')
includes('../../lib/commonlibsse-ng')

set_project('InterruptPackageProbe')
set_version('0.5.0')
set_license('GPL-3.0-or-later')

set_languages('c++23')
set_warnings('allextra')
set_toolset('msvc', 'ninja')

add_rules('mode.release')

target('InterruptPackageProbe')
    add_deps('commonlibsse-ng')
    add_rules('commonlib.plugin')
    on_load(function(target)
        target:data_set('commonlib.plugin.config', {
            name        = 'InterruptPackageProbe',
            author      = 'KezC7',
            description = "Log-only: finds Actor::EndInterruptPackage's callers (patches nothing)."
        })
    end)
    add_files('probe.cpp')
