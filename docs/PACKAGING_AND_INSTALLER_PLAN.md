# Plan: Packaging, Installers, Licensing, Copyright & Code-Signing

## Context

The current CPack config produces a `.dmg` on macOS, a raw `.zip` on Windows, and a `.tar.gz` on Linux — all unsigned, with no proper installer experience and no copyright metadata in the Windows binary. This plan replaces the ZIP with a full NSIS wizard installer, adds a portable AppImage for Linux alongside the TGZ, hardens the macOS DMG (SLA, volume icon, bundle copyright, notarization), embeds Windows VERSIONINFO with copyright/version strings, and wires optional code-signing for all three platforms in CI — gated on GitHub Actions secrets so unsigned builds continue to work without any secrets configured.

The plan also ensures `macdeployqt` / `windeployqt` run correctly at both build time and install time and that GDAL libraries + data (`gdal/`, `proj/` share directories) are correctly bundled in the final packages.

---

## Critical Files

| File | Change |
|---|---|
| `CMakeLists.txt` | NSIS CPack vars, macOS bundle copyright + DMG SLA, install-time codesign fix, `include(GNUInstallDirs)` |
| `resources/swmmvis.rc` → **`swmmvis.rc.in`** | Convert to CMake template; add full VERSIONINFO block |
| `.github/workflows/build_and_test.yml` | NSIS install, signing steps, notarization, AppImage, expanded artifacts |
| `packaging/linux/org.openswmm.swmmvis.desktop` | **New file** — XDG desktop entry (required by linuxdeploy) |
| `packaging/linux/org.openswmm.swmmvis.metainfo.xml` | **New file** — AppStream metadata |
| `resources/swmmvis.png` | **New file** — 512×512 PNG app icon (required by linuxdeploy) |

---

## Implementation Order

### Step 1 — `resources/swmmvis.rc.in` (replace `resources/swmmvis.rc`)

Convert the existing one-liner to a full template. The generated file is written back to `${PROJECT_SOURCE_DIR}/resources/swmmvis.rc` (same pattern as `version.h.in`) so the icon relative path `"swmmvis.ico"` continues to resolve.

**Content structure:**
```
IDI_ICON1  ICON  "swmmvis.ico"

VS_VERSION_INFO VERSIONINFO
  FILEVERSION     @PROJECT_VERSION_MAJOR@,@PROJECT_VERSION_MINOR@,@PROJECT_VERSION_PATCH@,0
  PRODUCTVERSION  @PROJECT_VERSION_MAJOR@,@PROJECT_VERSION_MINOR@,@PROJECT_VERSION_PATCH@,0
  FILETYPE        VFT_APP
  ...
  BLOCK "StringFileInfo"
    BLOCK "040904b0"
      VALUE "FileDescription",  "OpenSWMM Visualization Tool"
      VALUE "FileVersion",      "@PROJECT_VERSION@@PROJECT_VERSION_SUFFIX@"
      VALUE "ProductVersion",   "@PROJECT_VERSION@@PROJECT_VERSION_SUFFIX@"
      VALUE "ProductName",      "SWMMVis"
      VALUE "CompanyName",      "Caleb Buahin"
      VALUE "LegalCopyright",   "Copyright (C) 2026 Caleb Buahin. Licensed under GPLv3."
      VALUE "OriginalFilename", "SWMMVis.exe"
      VALUE "InternalName",     "SWMMVis"
  BLOCK "VarFileInfo"
    VALUE "Translation", 0x0409 0x04b0
```

Use `@ONLY` flag in `configure_file` — the RC block syntax uses `{` which would clash with CMake `${}` substitution without `@ONLY`.

---

### Step 2 — `resources/swmmvis.png`

Add a 512×512 square PNG app icon (export from `swmmvis.icns`). Required by `linuxdeploy --icon-file`. Committed binary asset — no generation step needed.

---

### Step 3 — `packaging/linux/org.openswmm.swmmvis.desktop`

```ini
[Desktop Entry]
Version=1.0
Type=Application
Name=SWMMVis
GenericName=SWMM Visualization Tool
Comment=A SWMM model setup, analysis, and visualization tool
Exec=SWMMVis %F
Icon=org.openswmm.swmmvis
Categories=Science;Education;
MimeType=application/x-swmm;
StartupWMClass=SWMMVis
StartupNotify=true
```

`linuxdeploy` reads this file to determine the app name, icon name, and categories for the AppDir structure. The icon name `org.openswmm.swmmvis` is matched to `resources/swmmvis.png` via `--icon-file`.

---

### Step 4 — `packaging/linux/org.openswmm.swmmvis.metainfo.xml`

AppStream metadata for software centre discoverability. Key fields:
- `<id>org.openswmm.swmmvis</id>`
- `<metadata_license>CC0-1.0</metadata_license>` (license for the XML itself)
- `<project_license>GPL-3.0-or-later</project_license>`
- `<launchable type="desktop-id">org.openswmm.swmmvis.desktop</launchable>` — must match the desktop file name exactly
- `<content_rating type="oars-1.1"/>` — no child elements = "all ages"

---

### Step 5 — `CMakeLists.txt` changes

#### 5a. Add `include(GNUInstallDirs)` near the top

After `cmake_minimum_required`, before the first `install()` call. Without this, `CMAKE_INSTALL_BINDIR` / `CMAKE_INSTALL_LIBDIR` are undefined and the NSIS `CPACK_NSIS_INSTALLED_ICON_NAME` path (`bin\\SWMMVis.exe`) would be wrong. Also affects Windows GDAL data install rules which use `${CMAKE_INSTALL_BINDIR}/gdal`.

#### 5b. Add `configure_file` for `swmmvis.rc.in`

After the existing `configure_file` for `version.h.in`:
```cmake
if(WIN32)
    configure_file(
        ${PROJECT_SOURCE_DIR}/resources/swmmvis.rc.in
        ${PROJECT_SOURCE_DIR}/resources/swmmvis.rc
        @ONLY
    )
endif()
```

#### 5c. Add `MACOSX_BUNDLE_COPYRIGHT` to `set_target_properties`

```cmake
MACOSX_BUNDLE_COPYRIGHT "Copyright (C) 2026 Caleb Buahin. Licensed under GPLv3."
```

This populates `NSHumanReadableCopyright` in the generated `Info.plist`.

#### 5d. macOS: Verify macdeployqt and GDAL bundling

The existing POST_BUILD `macdeployqt` step bundles Qt frameworks + all linked dylibs (incl. GDAL and its transitive deps). `install(TARGETS SWMMVis BUNDLE DESTINATION .)` copies a **clean** bundle (without POST_BUILD modifications) to the dist tree, and the existing `install(CODE "execute_process(COMMAND macdeployqt ...)")` re-runs macdeployqt on the installed bundle.

**Gap — re-sign after install-time macdeployqt**: After the install-time `macdeployqt`, the bundle's code directory is invalidated because GDAL/PROJ dylibs get newly embedded. Add a second `install(CODE ...)` block that re-signs using `--options runtime --timestamp` (required for notarization; `--deep` is deprecated for this purpose):

```cmake
# After the existing macdeployqt install(CODE) block:
install(CODE
    "execute_process(COMMAND
        codesign --force --sign \"${_codesign_identity}\"
        --options runtime --timestamp
        \"\${CMAKE_INSTALL_PREFIX}/SWMMVis.app\")"
    COMPONENT Runtime
)
```

**GDAL/PROJ data on macOS**: The existing `copy_directory` POST_BUILD commands copy `share/gdal/` and `share/proj/` into `Contents/Resources/gdal/` and `Contents/Resources/proj/`. Since `install(TARGETS SWMMVis BUNDLE DESTINATION .)` copies the whole `.app` (including `Contents/Resources/`), these data dirs are automatically included — no additional install rule needed.

#### 5e. Windows: Verify windeployqt and GDAL bundling

The existing `install(CODE "execute_process(COMMAND windeployqt ...)")` runs on the installed `.exe` to copy Qt DLLs and plugins. The vcpkg `bin/` DLLs (GDAL, PROJ, GEOS, libcurl, etc.) are handled by:

```cmake
install(DIRECTORY "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/"
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    FILES_MATCHING PATTERN "*.dll"
)
```

The GDAL/PROJ share data is handled by:
```cmake
install(DIRECTORY "${_SWMMVIS_GDAL_SHARE}" DESTINATION ${CMAKE_INSTALL_BINDIR}/gdal COMPONENT Runtime)
install(DIRECTORY "${_SWMMVIS_PROJ_SHARE}" DESTINATION ${CMAKE_INSTALL_BINDIR}/proj COMPONENT Runtime)
```

With `include(GNUInstallDirs)` (Step 5a), `CMAKE_INSTALL_BINDIR` = `bin`, so these install to `bin/gdal` and `bin/proj` relative to the install prefix — CPack picks them up correctly. **No additional changes needed** once Step 5a is in place.

#### 5f. CPack: macOS DMG enhancements

```cmake
if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME   "SWMMVis ${PROJECT_VERSION}")
    set(CPACK_DMG_SLA_FILE      "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_DMG_VOLUME_ICON   "${CMAKE_CURRENT_SOURCE_DIR}/resources/swmmvis.icns")
```

- `CPACK_DMG_SLA_FILE` — displays GPLv3 text when user mounts the DMG (GPLv3 compliance). CMake 3.21+ handles the `hdiutil` SLA resource embedding automatically — safe.
- `CPACK_DMG_VOLUME_ICON` — shows the SWMMVis icon in Finder's sidebar when the DMG is mounted.

#### 5g. CPack: Windows — Replace ZIP with NSIS

```cmake
elseif(WIN32)
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_NSIS_PACKAGE_NAME         "SWMMVis ${PROJECT_VERSION}${PROJECT_VERSION_SUFFIX}")
    set(CPACK_NSIS_DISPLAY_NAME         "SWMMVis ${PROJECT_VERSION}${PROJECT_VERSION_SUFFIX}")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "SWMMVis")
    set(CPACK_NSIS_INSTALL_ROOT         "$PROGRAMFILES64")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH          OFF)
    set(CPACK_NSIS_URL_INFO_ABOUT       "https://github.com/hydrocouple/openswmm.gui")
    set(CPACK_NSIS_CONTACT              "calebgh@gmail.com")
    set(CPACK_NSIS_HELP_LINK            "https://hydrocouple.org/openswmm.gui")
    set(CPACK_NSIS_INSTALLED_ICON_NAME  "bin\\\\SWMMVis.exe")
    set(CPACK_NSIS_MUI_ICON             "${CMAKE_CURRENT_SOURCE_DIR}/resources/swmmvis.ico")
    set(CPACK_NSIS_MUI_UNIICON          "${CMAKE_CURRENT_SOURCE_DIR}/resources/swmmvis.ico")
    set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "CreateShortcut '$DESKTOP\\\\SWMMVis.lnk' '$INSTDIR\\\\bin\\\\SWMMVis.exe'")
    set(CPACK_NSIS_DELETE_ICONS_EXTRA
        "Delete '$DESKTOP\\\\SWMMVis.lnk'")
```

**License page**: automatic — `CPACK_RESOURCE_FILE_LICENSE` is already set to `LICENSE`.

**Key gotchas:**
- `CPACK_NSIS_CREATE_ICONS_EXTRA` is raw NSIS script; backslashes need `\\\\` in CMake string (becomes `\\` in the `.nsi` file, which NSIS interprets as `\`).
- `CPACK_NSIS_INSTALLED_ICON_NAME` path uses `bin\\\\SWMMVis.exe` (NSIS backslash, double-escaped for CMake).

#### 5h. CPack: Linux — Add TGZ comment

```cmake
else()
    # TGZ for direct distribution. Portable AppImage is built in CI via linuxdeploy.
    set(CPACK_GENERATOR "TGZ")
endif()
```

---

### Step 6 — `.github/workflows/build_and_test.yml` changes

#### 6a. Add `appimage_arch` to Linux matrix entry
```yaml
- os: ubuntu-latest
  ...
  appimage_arch: x86_64
```

#### 6b. Add "Install NSIS (Windows)" step
Before Configure, after system deps steps:
```yaml
- name: Install NSIS (Windows)
  if: runner.os == 'Windows'
  run: choco install nsis --no-progress -y
```
Must be before `cmake --preset` because CPack verifies `makensis.exe` is available at configure time.

#### 6c. Add "Set version env var" step (all platforms)
```yaml
- name: Set version env var
  run: |
    VERSION=$(grep -m1 'project.*VERSION' CMakeLists.txt | grep -oP 'VERSION \K[0-9]+\.[0-9]+\.[0-9]+')
    SUFFIX=$(grep -m1 'PROJECT_VERSION_SUFFIX' CMakeLists.txt | grep -oP '"[^"]+"' | tr -d '"')
    echo "SWMMVIS_VERSION=${VERSION}${SUFFIX}" >> $GITHUB_ENV
  shell: bash
```

#### 6d–6h. Signing steps — commented out initially

The macOS certificate import (6d), Configure identity injection (6e), Windows Authenticode signing (6f), macOS notarization (6g), macOS keychain cleanup (6h), and Linux GPG signing (6j) steps will be **added to the workflow but commented out** in the initial implementation. They are fully written and documented — uncomment + configure the corresponding secrets to activate signing for a release build.

#### 6d. macOS: Import Developer ID certificate (before Configure)
```yaml
- name: Import macOS Developer ID certificate
  if: runner.os == 'macOS' && env.MACOS_DEVELOPER_ID_CERT_BASE64 != ''
  env:
    MACOS_DEVELOPER_ID_CERT_BASE64:   ${{ secrets.MACOS_DEVELOPER_ID_CERT_BASE64 }}
    MACOS_DEVELOPER_ID_CERT_PASSWORD: ${{ secrets.MACOS_DEVELOPER_ID_CERT_PASSWORD }}
  run: |
    CERT_PATH=$RUNNER_TEMP/swmmvis_devid.p12
    KEYCHAIN_PATH=$RUNNER_TEMP/swmmvis.keychain-db
    KEYCHAIN_PASSWORD=$(openssl rand -base64 32)
    echo "$MACOS_DEVELOPER_ID_CERT_BASE64" | base64 --decode > "$CERT_PATH"
    security create-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN_PATH"
    security set-keychain-settings -lut 21600 "$KEYCHAIN_PATH"
    security unlock-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN_PATH"
    security import "$CERT_PATH" -P "$MACOS_DEVELOPER_ID_CERT_PASSWORD" \
        -A -t cert -f pkcs12 -k "$KEYCHAIN_PATH"
    security list-keychains -d user -s "$KEYCHAIN_PATH" $(security list-keychains -d user | tr -d '"')
    security set-key-partition-list -S apple-tool:,apple: -s -k "$KEYCHAIN_PASSWORD" "$KEYCHAIN_PATH"
    CODESIGN_IDENTITY=$(security find-identity -v -p codesigning "$KEYCHAIN_PATH" | \
        grep "Developer ID Application" | head -1 | sed 's/.*"\(.*\)".*/\1/')
    echo "CODESIGN_IDENTITY=$CODESIGN_IDENTITY" >> "$GITHUB_ENV"
    rm -f "$CERT_PATH"
```

#### 6e. Update Configure step to pass `-DCODESIGN_IDENTITY`
```yaml
- name: Configure
  run: |
    EXTRA=""
    if [ -n "$CODESIGN_IDENTITY" ]; then EXTRA="-DCODESIGN_IDENTITY=$CODESIGN_IDENTITY"; fi
    cmake --preset=${{ matrix.cmake_preset }} -DSWMMVIS_BUILD_TESTS=ON $EXTRA
  shell: bash
```
`CODESIGN_IDENTITY` is baked into `cmake_install.cmake` at configure time — it must be set before this step. Without secrets, it's empty and the existing ad-hoc (`-`) signing fires on macOS.

#### 6f. Windows: Authenticode signing (after Package)
```yaml
- name: Sign Windows installer (Authenticode)
  if: runner.os == 'Windows' && env.WINDOWS_CODESIGN_CERT_PFX_BASE64 != ''
  env:
    WINDOWS_CODESIGN_CERT_PFX_BASE64: ${{ secrets.WINDOWS_CODESIGN_CERT_PFX_BASE64 }}
    WINDOWS_CODESIGN_CERT_PASSWORD:   ${{ secrets.WINDOWS_CODESIGN_CERT_PASSWORD }}
  run: |
    $certBytes = [Convert]::FromBase64String($env:WINDOWS_CODESIGN_CERT_PFX_BASE64)
    $certPath  = Join-Path $env:RUNNER_TEMP "swmmvis_codesign.pfx"
    [IO.File]::WriteAllBytes($certPath, $certBytes)
    Get-ChildItem "${{ github.workspace }}\packages\*.exe" | ForEach-Object {
      & signtool sign /fd sha256 /tr http://timestamp.digicert.com /td sha256 `
          /f $certPath /p $env:WINDOWS_CODESIGN_CERT_PASSWORD $_.FullName
    }
    Remove-Item $certPath -Force
  shell: pwsh
```
PFX is written to `$RUNNER_TEMP` (not workspace) and deleted immediately to prevent accidental artifact capture.

#### 6g. macOS: Notarize + staple DMG (after Package, before Upload)
```yaml
- name: Notarize macOS DMG
  if: runner.os == 'macOS' && env.MACOS_NOTARYTOOL_APPLE_ID != ''
  env:
    MACOS_NOTARYTOOL_APPLE_ID:              ${{ secrets.MACOS_NOTARYTOOL_APPLE_ID }}
    MACOS_NOTARYTOOL_TEAM_ID:               ${{ secrets.MACOS_NOTARYTOOL_TEAM_ID }}
    MACOS_NOTARYTOOL_APP_SPECIFIC_PASSWORD: ${{ secrets.MACOS_NOTARYTOOL_APP_SPECIFIC_PASSWORD }}
  run: |
    DMG=$(find ${{ github.workspace }}/packages -name "*.dmg" | head -1)
    xcrun notarytool submit "$DMG" \
        --apple-id "$MACOS_NOTARYTOOL_APPLE_ID" \
        --team-id  "$MACOS_NOTARYTOOL_TEAM_ID" \
        --password "$MACOS_NOTARYTOOL_APP_SPECIFIC_PASSWORD" \
        --wait --output-format plist > notarization_result.plist
    STATUS=$(plutil -extract status raw notarization_result.plist)
    [ "$STATUS" = "Accepted" ] || { cat notarization_result.plist; exit 1; }
    xcrun stapler staple "$DMG"
```
`--wait` blocks 2–10 minutes. Staple must complete **before** the artifact upload step.

#### 6h. macOS: Keychain cleanup (always runs)
```yaml
- name: Cleanup macOS keychain
  if: always() && runner.os == 'macOS'
  run: security delete-keychain $RUNNER_TEMP/swmmvis.keychain-db 2>/dev/null || true
```

#### 6i. Linux: Build AppImage (after Install)
```yaml
- name: Build AppImage (Linux)
  if: runner.os == 'Linux'
  env:
    APPIMAGE_EXTRACT_AND_RUN: "1"   # bypass FUSE requirement on ubuntu-latest 22.04+
  run: |
    wget -q "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
    wget -q "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
    chmod +x linuxdeploy-x86_64.AppImage linuxdeploy-plugin-qt-x86_64.AppImage

    # Build AppDir from install tree (FHS layout: usr/bin, usr/lib, etc.)
    mkdir -p AppDir/usr
    cp -r ${{ github.workspace }}/dist/linux/* AppDir/usr/

    # vcpkg .so files are in dist/linux/lib/ — expose to ldd so linuxdeploy finds them
    # GDAL/PROJ data in dist/linux/bin/gdal and bin/proj is copied verbatim into AppDir
    export LD_LIBRARY_PATH=${{ github.workspace }}/AppDir/usr/lib:$LD_LIBRARY_PATH
    export QMAKE=$QT_ROOT_DIR/bin/qmake

    ./linuxdeploy-x86_64.AppImage \
        --appdir AppDir \
        --executable AppDir/usr/bin/SWMMVis \
        --desktop-file packaging/linux/org.openswmm.swmmvis.desktop \
        --icon-file resources/swmmvis.png \
        --plugin qt \
        --output appimage

    mkdir -p packages
    mv SWMMVis-*.AppImage packages/
```

**GDAL/PROJ data in AppImage**: The dist tree includes `bin/gdal/` and `bin/proj/` from the `install(DIRECTORY ...)` rules in CMakeLists.txt. `cp -r dist/linux/* AppDir/usr/` copies these verbatim, so linuxdeploy bundles them automatically.

#### 6j. Optional: GPG-sign AppImage
```yaml
- name: GPG-sign AppImage (Linux)
  if: runner.os == 'Linux' && env.GPG_PRIVATE_KEY != ''
  env:
    GPG_PRIVATE_KEY: ${{ secrets.GPG_PRIVATE_KEY }}
  run: |
    echo "$GPG_PRIVATE_KEY" | gpg --batch --import
    APPIMAGE=$(find packages -name "*.AppImage" | head -1)
    gpg --batch --yes --detach-sign --armor "$APPIMAGE"
    mv "$APPIMAGE.asc" packages/
```

#### 6k. Upload artifacts
```yaml
- name: Upload artifacts
  if: always()
  uses: actions/upload-artifact@v4
  with:
    name: swmmvis-${{ matrix.vcpkg_triplet }}
    path: packages/
```
Both TGZ and AppImage are moved to `packages/` — this single glob captures all artifacts.

---

## Required GitHub Actions Secrets

All signing steps are individually gated — the workflow succeeds without any secrets (unsigned packages produced).

| Secret | Platform | Purpose |
|---|---|---|
| `MACOS_DEVELOPER_ID_CERT_BASE64` | macOS | Base64-encoded `.p12` Developer ID Application cert |
| `MACOS_DEVELOPER_ID_CERT_PASSWORD` | macOS | Password for the `.p12` |
| `MACOS_NOTARYTOOL_APPLE_ID` | macOS | Apple ID email for notarytool |
| `MACOS_NOTARYTOOL_TEAM_ID` | macOS | 10-char Apple Developer Team ID |
| `MACOS_NOTARYTOOL_APP_SPECIFIC_PASSWORD` | macOS | App-specific password from appleid.apple.com |
| `WINDOWS_CODESIGN_CERT_PFX_BASE64` | Windows | Base64-encoded `.pfx` Authenticode cert |
| `WINDOWS_CODESIGN_CERT_PASSWORD` | Windows | Password for the `.pfx` |
| `GPG_PRIVATE_KEY` | Linux | ASCII-armored GPG private key (optional) |

---

## Key Gotchas

| Area | Gotcha | Fix |
|---|---|---|
| `GNUInstallDirs` missing | `CMAKE_INSTALL_BINDIR` undefined — NSIS icon path and GDAL install paths break | Add `include(GNUInstallDirs)` near top of CMakeLists.txt |
| RC `configure_file` | RC block syntax uses `{` — clashes with CMake `${}` | Use `@ONLY` flag |
| NSIS path separators | `CPACK_NSIS_CREATE_ICONS_EXTRA` is raw NSIS script; backslashes need `\\\\` in CMake string | Use `\\\\` in CMake string literals |
| macOS `--deep` signing | `codesign --deep` deprecated for notarization (macOS 12+) | Use `--options runtime --timestamp` in install-time sign block |
| macOS install-time re-sign | `cmake --install` copies clean bundle; after macdeployqt re-embeds GDAL dylibs, bundle signature is invalidated | Add second `install(CODE codesign ...)` after macdeployqt install code block |
| macOS staple order | Must staple DMG **before** artifact upload | Notarize → staple → upload step ordering |
| macOS GDAL data | `Contents/Resources/gdal/` and `proj/` set by POST_BUILD; `install(BUNDLE)` captures whole `.app` | Already correct — no extra rule needed |
| Windows GDAL data | `install(DIRECTORY ...)` uses `${CMAKE_INSTALL_BINDIR}/gdal` — needs `GNUInstallDirs` | Fixed by Step 5a |
| linuxdeploy FUSE | AppImage self-extractors need FUSE 2; not available on ubuntu-latest 22.04+ | `APPIMAGE_EXTRACT_AND_RUN=1` env var |
| linuxdeploy vcpkg libs | `ldd` scan may miss vcpkg `.so` files not in system lib paths | Set `LD_LIBRARY_PATH=AppDir/usr/lib:...` before running linuxdeploy |
| linuxdeploy icon | Requires `.png` or `.svg`; project only has `.icns` and `.ico` | Add `resources/swmmvis.png` to repo |
| linuxdeploy `qt.conf` | Plugin overwrites deployed `qt.conf` with AppImage-relative one | Expected; this is correct behavior |
| AppStream `<launchable>` | Must match desktop file name exactly | Both use `org.openswmm.swmmvis.desktop` |
| DMG SLA format | Requires CMake 3.17+ for `hdiutil` SLA embedding | Project requires 3.21+ — safe |
| NSIS license page | Automatic when `CPACK_RESOURCE_FILE_LICENSE` is set | Already set at line 725 — no extra work needed |
| macOS notarytool | `altool` deprecated in Xcode 14; use `notarytool` | Step 6g uses `xcrun notarytool` |

---

## Verification

1. **Windows — local**: Install NSIS, then `cmake --preset=Windows`, build, `cmake --install build/windows --prefix dist/windows`, `cpack --config build/windows/CPackConfig.cmake -B packages` → `SWMMVis-6.0.0-alpha.1-win64.exe`. Run installer: verify license page shows GPLv3, verify `SWMMVis.exe` Properties → Details tab shows copyright and version.
2. **macOS — local**: `cmake --preset=Darwin` (add `-DCODESIGN_IDENTITY=-` for ad-hoc), install, cpack → `.dmg`. Mount: verify GPLv3 SLA appears and SWMMVis icon shows in Finder sidebar. Run app: verify About box shows copyright.
3. **Linux — local**: `cmake --preset=Linux`, install, cpack → `.tar.gz`. Run linuxdeploy step manually → `SWMMVis-6.0.0-alpha.1-x86_64.AppImage`. `chmod +x`, run — verify GDAL layers work.
4. **CI without secrets**: Push to `dev` → all three platform jobs succeed, produce unsigned `.exe`, `.dmg`, `.tar.gz`, `.AppImage`.
5. **CI with signing secrets**: Windows job signs `.exe` (verify with `signtool verify /pa`); macOS job produces notarized+stapled `.dmg` (verify with `spctl --assess --type open --context context:primary-signature`).
