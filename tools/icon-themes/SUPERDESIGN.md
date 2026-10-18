# Superdesign Application Icons

Separate fifth modern theme, ID `superdesign`. Default only for fresh/unset
application icon preferences. Saved theme IDs remain selected; the existing Apex
alias still resolves to Reborn. Original, Faenza, Haiku, Monochrome, Prism,
Contour, Slate and Reborn remain available, with unchanged assets.

The 172 new SVGs use Lucide 1.8.0 rounded 24x24 outlines and explicit light/dark
accent colors based on the approved compact-native design. Torrent follows
the selected cloud-download direction. Original branding/tray icons fall back
to Apex, and user/avatar sets are not changed.

## Offline Sources

`vendor-lucide/` holds 79 unmodified ESM source modules, their extracted node
arrays, complete upstream license (including Feather notices), and SHA-256
provenance. Imported from the bundled workspace dependency, without network
access, installation, or package scripts. The source package is `lucide@1.8.0`
from https://github.com/lucide-icons/lucide. Keep LICENSE with derived assets.
The regression verifies extracted nodes against every retained source module.

The old Phosphor vendor bank, catalog, generator, and manifest are independent
and unchanged. This generator writes only the Superdesign directory and refuses
unowned files, symlinks, and modified outputs.

```sh
node tools/icon-themes/generate-superdesign.mjs
node tools/icon-themes/generate-superdesign.mjs --check
node --test tools/icon-themes/test-superdesign.mjs
cmake -S tools/icon-themes/superdesign-tests -B /tmp/eiskalt-superdesign-tests
cmake --build /tmp/eiskalt-superdesign-tests -j4
ctest --test-dir /tmp/eiskalt-superdesign-tests --output-on-failure
```

The standalone native target requires Qt6 Core/Gui/Widgets/Svg, not Catch2,
network fetches, or the shared application build. It links actual production
AppIconTheme code and the production resource registration function. Parent
already registers `tests/qt/test_appicontheme.cpp`; its default/theme assertions
are updated in place with authorization. No new shared CMake entry is required.
`test_superdesignicons.cpp` has its own main and belongs only to the standalone
target, not the shared Qt executable. No layout/auth files are part of this slice.
