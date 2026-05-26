#!/usr/bin/env python3
"""
Cross-platform release builder for Seelie.

Auto-detects Qt, build tools, and platform-specific packagers:
  - Windows: Inno Setup (ISCC) -> .exe installer via scripts/build_inno.py
  - macOS:   macdeployqt + hdiutil -> .dmg
  - Linux:   cmake install + appimagetool -> .AppImage

Usage:
    python scripts/build_release.py
    python scripts/build_release.py --build-dir build-rel --config Release
"""

import argparse
import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent


def run(cmd, **kwargs):
    """Run a command, printing it first."""
    cmd_str = " ".join(str(c) for c in cmd)
    print(f"  -> {cmd_str}")
    kwargs.setdefault("check", True)
    return subprocess.run(cmd, **kwargs)


def run_output(cmd, **kwargs):
    """Run a command and return stdout."""
    result = subprocess.run(cmd, capture_output=True, text=True, check=False, **kwargs)
    return result.stdout.strip() if result.returncode == 0 else ""


def check_program(name, hint=""):
    """Check if a program exists in PATH."""
    path = shutil.which(name)
    if path:
        print(f"  [OK] {name}: {path}")
        return Path(path)
    msg = f"  [MISSING] {name} not found in PATH"
    if hint:
        msg += f" ({hint})"
    print(msg)
    return None


def get_project_version():
    """Parse version from top-level CMakeLists.txt."""
    cmake = PROJECT_ROOT / "CMakeLists.txt"
    if not cmake.exists():
        return "1.0.0"
    text = cmake.read_text(encoding="utf-8")
    m = re.search(r"project\([^)]*VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)", text)
    return m.group(1) if m else "1.0.0"


def get_cached_qt_prefix(build_dir):
    """If a CMakeCache exists, return the Qt prefix that was last configured."""
    cache = build_dir / "CMakeCache.txt"
    if not cache.exists():
        return None
    text = cache.read_text(errors="ignore")
    m = re.search(r"^Qt6_DIR:PATH=(.+)/lib/cmake/Qt6$", text, re.MULTILINE)
    if m:
        p = Path(m.group(1))
        if p.exists():
            return p
    return None


def find_qt_prefix(cached_prefix=None):
    """
    Detect Qt 6 installation prefix.
    Returns Path(prefix) or None.
    """
    # 0. Prefer the Qt that was already used for an existing build
    if cached_prefix:
        print(f"  [OK] Qt prefix from existing build cache: {cached_prefix}")
        return cached_prefix

    # 1. Environment variables
    for env in ("QT_DIR", "QT6_DIR", "Qt6_DIR", "QT_ROOT", "QT_INSTALL_PREFIX"):
        val = os.environ.get(env)
        if val:
            p = Path(val)
            if p.exists():
                print(f"  [OK] Qt prefix from {env}: {p}")
                return p

    # 2. Filesystem locations (preferred over PATH because the official Qt
    # installer at /opt/Qt or C:\Qt ships a more complete plugin set than
    # Homebrew/apt bottles — notably libffmpegmediaplugin.dylib, which is
    # the only multimedia backend that can decode raw MP3 via QAudioDecoder
    # on macOS. PATH-resolved `qmake` is Homebrew/apt by default and would
    # silently win otherwise, breaking TTS in production builds.
    home = Path.home()
    candidates = []
    if sys.platform == "win32":
        candidates = [Path(r"C:\Qt"), home / "Qt"]
    elif sys.platform == "darwin":
        candidates = [Path("/opt/Qt"), Path("/Applications/Qt"), home / "Qt"]
    else:
        candidates = [Path("/opt/Qt"), home / "Qt"]

    if sys.platform == "win32":
        host_prefixes = ("mingw", "llvm-mingw", "msvc")
    elif sys.platform == "darwin":
        host_prefixes = ("macos", "clang_64")
    else:
        host_prefixes = ("gcc_64", "linux")

    def _kit_rank(d):
        name = d.name.lower()
        for i, pref in enumerate(host_prefixes):
            if name.startswith(pref):
                return (0, i, name)
        return (1, 0, name)

    for base in candidates:
        if not base.exists():
            continue
        versions = sorted(
            (d for d in base.iterdir() if d.is_dir() and d.name.startswith("6.")),
            key=lambda d: d.name,
            reverse=True,
        )
        for v in versions:
            kits = sorted((d for d in v.iterdir() if d.is_dir()), key=_kit_rank)
            for sub in kits:
                bin_dir = sub / "bin"
                if sys.platform == "win32":
                    names = ("qtpaths6.exe", "qtpaths.exe", "qmake6.exe", "qmake.exe")
                else:
                    names = ("qtpaths6", "qtpaths", "qmake6", "qmake")
                if any((bin_dir / name).exists() for name in names):
                    print(f"  [OK] Qt prefix from filesystem search: {sub}")
                    return sub

    # 3. Query tools in PATH (last resort; on macOS this finds Homebrew Qt,
    # which lacks the ffmpeg multimedia backend — see comment on step 2).
    for tool in ("qtpaths6", "qtpaths", "qmake6", "qmake"):
        exe = shutil.which(tool)
        if not exe:
            continue
        try:
            if tool.startswith("qtpaths"):
                out = run_output([exe, "--query", "QT_INSTALL_PREFIX"])
            else:
                # Verify it's Qt 6
                ver = run_output([exe, "-query", "QT_VERSION"])
                if ver and not ver.startswith("6."):
                    continue
                out = run_output([exe, "-query", "QT_INSTALL_PREFIX"])
            if out:
                p = Path(out)
                if p.exists():
                    print(f"  [OK] Qt prefix from {tool}: {p}")
                    return p
        except Exception:
            continue

    # 4. Interactive prompt — last resort before giving up. Auto-discovery
    # failed entirely; ask the user where Qt lives rather than continue
    # with a broken state.
    return _prompt_for_qt_prefix()


def _prompt_for_qt_prefix():
    """Interactively ask the user where Qt is installed.

    Triggered when none of the standard discovery paths worked. We want
    this to be loud and explicit — a silent fallback to "no Qt found"
    leads to confusing CMake errors several screens later.
    """
    print()
    print("=" * 70)
    print("  Qt 6 installation could not be auto-discovered.")
    print("=" * 70)
    print("  Checked: $QT_DIR / $Qt6_DIR env vars, conventional install roots,")
    print("           qmake/qtpaths on PATH.")
    print()
    print("  Paste the path to the Qt kit you want to build against. The kit")
    print("  is the directory that *contains* the bin/ folder (i.e. the one")
    print("  with bin/qmake under it). Conventional locations from the Qt")
    print("  Online Installer / Maintenance Tool:")
    if sys.platform == "darwin":
        print("    <Qt-install-root>/<version>/macos")
    elif sys.platform == "win32":
        print(r"    <Qt-install-root>\<version>\mingw_64")
        print(r"    <Qt-install-root>\<version>\msvc2022_64")
    else:
        print("    <Qt-install-root>/<version>/gcc_64")
    print()
    print("  Leave blank to abort.")
    print("=" * 70)
    try:
        answer = input("  Qt prefix: ").strip()
    except (EOFError, KeyboardInterrupt):
        print()
        return None
    if not answer:
        return None
    p = Path(answer).expanduser()
    if not p.exists():
        print(f"  [ERROR] Path does not exist: {p}")
        return None
    # Sanity-check: prefix must contain a qmake/qtpaths binary.
    bin_dir = p / "bin"
    if sys.platform == "win32":
        names = ("qtpaths6.exe", "qtpaths.exe", "qmake6.exe", "qmake.exe")
    else:
        names = ("qtpaths6", "qtpaths", "qmake6", "qmake")
    if not any((bin_dir / name).exists() for name in names):
        print(f"  [ERROR] {p} does not look like a Qt prefix — no qmake/qtpaths under bin/")
        print(f"          The prefix should be the directory that *contains* the bin/ folder.")
        return None
    print(f"  [OK] Qt prefix from user input: {p}")
    return p


def find_cmake(qt_prefix=None):
    """Find cmake, preferring Qt-bundled copies."""
    # PATH
    path = shutil.which("cmake")
    if path:
        return Path(path)
    # Relative to Qt prefix Tools directory
    if qt_prefix:
        qt_root = qt_prefix
        for _ in range(2):
            qt_root = qt_root.parent
        tools = qt_root / "Tools"
        if tools.exists():
            # CMake_64/bin/cmake.exe or CMake/bin/cmake
            for cand in (tools / "CMake_64" / "bin" / "cmake", tools / "CMake" / "bin" / "cmake"):
                exe = cand.with_suffix(".exe") if sys.platform == "win32" else cand
                if exe.exists():
                    return exe
            # Generic search under Tools/*/bin/cmake
            for sub in tools.iterdir():
                if sub.is_dir():
                    exe = sub / "bin" / ("cmake.exe" if sys.platform == "win32" else "cmake")
                    if exe.exists():
                        return exe
    return None


def find_ninja(qt_prefix=None):
    """Find ninja, preferring Qt-bundled copies."""
    path = shutil.which("ninja")
    if path:
        return Path(path)
    if qt_prefix:
        qt_root = qt_prefix
        for _ in range(2):
            qt_root = qt_root.parent
        tools = qt_root / "Tools"
        if tools.exists():
            exe = tools / "Ninja" / ("ninja.exe" if sys.platform == "win32" else "ninja")
            if exe.exists():
                return exe
    return None


def find_macdeployqt(qt_prefix=None):
    """Find macdeployqt."""
    if qt_prefix:
        p = qt_prefix / "bin" / "macdeployqt"
        if p.exists():
            return p
    return shutil.which("macdeployqt")


def find_appimagetool():
    """Find appimagetool."""
    return shutil.which("appimagetool")


def find_linuxdeploy():
    """Find linuxdeploy (with AppImage output support)."""
    return shutil.which("linuxdeploy")





# ---------------------------------------------------------------------------
# Platform packaging
# ---------------------------------------------------------------------------

def package_windows(build_dir, version, qt_prefix, cmake, config="Release"):
    """Delegate to scripts/build_inno.py — Windows uses Inno Setup."""
    print("\n[Windows] Packaging with Inno Setup...")
    inno_script = SCRIPT_DIR / "build_inno.py"
    cmd = [sys.executable, str(inno_script), "--build-dir", str(build_dir.relative_to(PROJECT_ROOT))]
    # Forward --include-nsfw if it was on the parent's command line. Args
    # already parsed in main(); read from sys.argv here so we don't have
    # to thread it through.
    if "--include-nsfw" in sys.argv:
        cmd.append("--include-nsfw")
    run(cmd)


def package_macos(build_dir, version, qt_prefix):
    print("\n[macOS] Packaging DMG...")

    app_bundle = build_dir / "Seelie.app"
    if not app_bundle.exists():
        print(f"ERROR: {app_bundle} not found. Build may have failed.")
        sys.exit(1)

    macdeployqt = find_macdeployqt(qt_prefix)
    main_bin = app_bundle / "Contents" / "MacOS" / "Seelie"
    bin_backup = build_dir / "Seelie.unmangled"
    if main_bin.exists():
        # Save the freshly-linked binary. macdeployqt mangles it (deletes our
        # baked-in rpath, and partial install_name_tool ops corrupt segment
        # vmsizes so subsequent install_name_tool runs reject it as malformed).
        # We restore this clean copy after macdeployqt and rewrite framework
        # paths ourselves.
        shutil.copy2(main_bin, bin_backup)

    if macdeployqt:
        print(f"  [OK] macdeployqt: {macdeployqt}")
        # macdeployqt aborts on malformed QtQml/virtualkeyboard frameworks
        # but copies all frameworks into the bundle before failing — that's
        # all we need from it.
        #
        # CRITICAL: macdeployqt internally shells out to `qmake -query` to
        # discover plugin/lib paths. If Homebrew's qmake is on PATH, it wins
        # over our chosen Qt prefix and macdeployqt copies plugins from a
        # *different* Qt version into the bundle (e.g. Homebrew 6.11.0
        # arm64-only plugins next to /opt/Qt 6.11.1 universal frameworks).
        # The Qt plugin loader then rejects them on minor-version mismatch
        # — symptom: WEBP/SVG fail at runtime even though the .dylib exists.
        # Pin PATH so the qmake adjacent to our macdeployqt wins, AND so the
        # macOS native /usr/bin/strip wins over Homebrew GNU binutils' strip.
        # macdeployqt internally calls `strip` to shrink the copied frameworks;
        # GNU binutils strip doesn't understand Mach-O fat binaries and silently
        # corrupts every framework it touches, which cascades into the rest of
        # the script (install_name_tool: "no LC_ID_DYLIB load command", codesign
        # verification errors, etc.). Putting /usr/bin first ensures strip and
        # otool come from the Xcode toolchain.
        deploy_env = os.environ.copy()
        path_parts = ["/usr/bin", "/bin"]
        qt_bin = str(Path(qt_prefix) / "bin") if qt_prefix else None
        if qt_bin:
            path_parts.append(qt_bin)
        path_parts.append(deploy_env.get("PATH", ""))
        deploy_env["PATH"] = os.pathsep.join(p for p in path_parts if p)
        run([str(macdeployqt), str(app_bundle)], check=False, env=deploy_env)
    else:
        print("  [WARNING] macdeployqt not found – relying on cmake install deploy script.")

    # macdeployqt regenerates Info.plist and strips our LSUIElement.
    # Re-apply it after deployment so the Dock icon stays hidden.
    hide_script = PROJECT_ROOT / "scripts" / "hide_dock_icon.py"
    plist_path = app_bundle / "Contents" / "Info.plist"
    if hide_script.exists() and plist_path.exists():
        run([sys.executable, str(hide_script), str(plist_path)], check=False)

    # Restore the un-mangled binary on top of macdeployqt's broken one.
    if bin_backup.exists():
        shutil.copy2(bin_backup, main_bin)
        bin_backup.unlink()

    # Ensure packs are inside the bundle (CMake target_sources usually does this,
    # but CI also copies explicitly as a safeguard)
    packs_src = build_dir / "packs"
    packs_dst = app_bundle / "Contents" / "Resources" / "packs"
    if packs_src.exists():
        packs_dst.mkdir(parents=True, exist_ok=True)
        for spk in packs_src.glob("*.spk"):
            dst = packs_dst / spk.name
            if not dst.exists():
                shutil.copy2(spk, dst)
                print(f"  Copied pack: {spk.name}")

    # Strip stray files in Contents/MacOS/ — anything besides the executable
    # there invalidates the bundle's codesignature ("code object is not signed
    # at all") and LaunchServices refuses to launch the .app. Common culprit:
    # a dev-run wrote seelie_debug.log next to the binary.
    macos_dir = app_bundle / "Contents" / "MacOS"
    for entry in macos_dir.iterdir():
        if entry.name == "Seelie":
            continue
        print(f"  Removing stray bundle file: Contents/MacOS/{entry.name}")
        if entry.is_dir():
            shutil.rmtree(entry)
        else:
            entry.unlink()

    # Strip the QML / Quick / VirtualKeyboard families macdeployqt bundled.
    # We're a pure Widgets app, don't need any of these, and their malformed
    # Mach-O headers also break codesign and LaunchServices.
    for unwanted in [
        app_bundle / "Contents" / "PlugIns" / "platforminputcontexts",
        *app_bundle.glob("Contents/Frameworks/QtQml*.framework"),
        app_bundle / "Contents" / "Frameworks" / "QtQmlWorkerScript.framework",
        app_bundle / "Contents" / "Frameworks" / "QtQuick.framework",
        *app_bundle.glob("Contents/Frameworks/QtVirtualKeyboard*.framework"),
    ]:
        if unwanted.exists():
            shutil.rmtree(unwanted)
            print(f"  Removed unused: {unwanted.name}")

    # Rewrite framework references from absolute system paths to @rpath on
    # the restored binary, and add the bundle rpath to the cocoa plugin.
    # The main binary already has INSTALL_RPATH baked in by CMake, so we
    # only rewrite -L deps here. Strip ad-hoc signatures first because
    # install_name_tool refuses to touch signed Mach-Os.
    install_name_tool = shutil.which("install_name_tool")
    codesign_path = shutil.which("codesign")
    otool_path = shutil.which("otool")

    def _rewrite_to_rpath(target):
        if not (target.exists() and install_name_tool and otool_path):
            return
        if codesign_path:
            run([codesign_path, "--remove-signature", str(target)], check=False)
        deps = subprocess.run([otool_path, "-L", str(target)],
                              capture_output=True, text=True, check=False).stdout
        for line in deps.splitlines()[1:]:
            line = line.strip()
            if not line:
                continue
            dep = line.split(" (")[0]
            # Only rewrite Qt frameworks coming from system locations.
            if "Qt" not in dep:
                continue
            if not (dep.startswith("/opt/homebrew/") or dep.startswith("/usr/local/")
                    or dep.startswith("/Library/")):
                continue
            # Build the @rpath form from the framework path:
            # /opt/homebrew/opt/qtbase/lib/QtCore.framework/Versions/A/QtCore
            #   -> @rpath/QtCore.framework/Versions/A/QtCore
            idx = dep.rfind("/Qt")
            while idx != -1 and ".framework" not in dep[idx:idx+50]:
                idx = dep.rfind("/Qt", 0, idx)
            if idx == -1:
                continue
            new_dep = "@rpath" + dep[idx:]
            run([install_name_tool, "-change", dep, new_dep, str(target)],
                check=False)

    _rewrite_to_rpath(main_bin)

    # macdeployqt corrupts framework binaries when it bails — strips
    # LC_ID_DYLIB from QtCore.framework/Versions/A/QtCore etc, leaving
    # dyld with "MH_DYLIB is missing LC_ID_DYLIB" at launch. Re-copy each
    # framework's main binary from the system Qt prefix, set its ID to
    # the @rpath form, and rewrite its inter-framework deps to @rpath too.
    qt_lib_dir = Path(qt_prefix) / "lib"
    if not qt_lib_dir.exists():
        qt_lib_dir = Path(qt_prefix) / "share" / "qt" / "lib"
    bundle_fw = app_bundle / "Contents" / "Frameworks"
    if install_name_tool and qt_lib_dir.exists():
        for fw_dir in bundle_fw.glob("Qt*.framework"):
            fw_name = fw_dir.name.replace(".framework", "")
            src_bin = qt_lib_dir / fw_dir.name / "Versions" / "A" / fw_name
            dst_bin = fw_dir / "Versions" / "A" / fw_name
            if not src_bin.exists() or not dst_bin.exists():
                continue
            os.chmod(dst_bin, 0o644)
            shutil.copy2(src_bin, dst_bin)
            if codesign_path:
                run([codesign_path, "--remove-signature", str(dst_bin)], check=False)
            new_id = f"@rpath/{fw_dir.name}/Versions/A/{fw_name}"
            run([install_name_tool, "-id", new_id, str(dst_bin)], check=False)
            _rewrite_to_rpath(dst_bin)

    # Cocoa plugin needs an rpath added (Qt ships it with @loader_path/.../lib
    # which is wrong inside the bundle), AND its Qt deps rewritten. Recopy
    # the plugin binary from system Qt because macdeployqt's failed run
    # corrupts it (strips LC_ID_DYLIB just like the frameworks).
    cocoa = app_bundle / "Contents" / "PlugIns" / "platforms" / "libqcocoa.dylib"
    if install_name_tool and cocoa.exists():
        # Find the system source (Qt installs plugins under share/qt/plugins or
        # share/qt6/plugins on Homebrew, or plugins/ inside the prefix).
        for plug_root in [Path(qt_prefix) / "share" / "qt" / "plugins",
                          Path(qt_prefix) / "share" / "qt6" / "plugins",
                          Path(qt_prefix) / "plugins"]:
            src_cocoa = plug_root / "platforms" / "libqcocoa.dylib"
            if src_cocoa.exists():
                os.chmod(cocoa, 0o644)
                shutil.copy2(src_cocoa, cocoa)
                break
        if codesign_path:
            run([codesign_path, "--remove-signature", str(cocoa)], check=False)
        run([install_name_tool, "-id",
             "@rpath/PlugIns/platforms/libqcocoa.dylib", str(cocoa)],
            check=False)
        run([install_name_tool, "-add_rpath",
             "@loader_path/../../Frameworks", str(cocoa)],
            check=False)
        _rewrite_to_rpath(cocoa)

    # Fix imageformats plugins — macdeployqt on Qt 6.11 (Homebrew arm64)
    # writes truncated/byte-mangled Mach-O headers for every plugin in
    # PlugIns/imageformats/, and the same Qt's plugin loader then refuses
    # them with "invalid magic cefaedfe". PNG / JPEG only "appear to work"
    # because Qt has built-in fallbacks; WEBP has none, so Codex pets fail.
    # Recopy each plugin from the Qt source tree, rewrite its absolute
    # /opt/homebrew/... deps to @rpath, and add a Frameworks rpath.
    # Skip `install_name_tool -id` — plugins don't need an install ID, and
    # -id fails on MH_BUNDLE files (which is what qtimageformats ships).
    plug_roots = [Path(qt_prefix) / "plugins",
                  Path(qt_prefix) / "share" / "qt" / "plugins",
                  Path(qt_prefix) / "share" / "qt6" / "plugins"]

    def _find_source_plugin(rel_path):
        for root in plug_roots:
            candidate = root / rel_path
            if candidate.exists():
                return candidate
        return None

    def _redeploy_plugin(plugin_path, rel_under_plugins):
        if not (install_name_tool and otool_path):
            return False
        src = _find_source_plugin(rel_under_plugins)
        if not src:
            return False
        os.chmod(plugin_path, 0o644)
        shutil.copy2(src, plugin_path)
        if codesign_path:
            run([codesign_path, "--remove-signature", str(plugin_path)], check=False)
        deps = subprocess.run([otool_path, "-L", str(plugin_path)],
                              capture_output=True, text=True, check=False).stdout
        for line in deps.splitlines()[1:]:
            line = line.strip()
            if not line:
                continue
            dep = line.split(" (")[0]
            if not (dep.startswith("/opt/homebrew/") or dep.startswith("/usr/local/")):
                continue
            if "Qt" in dep:
                idx = dep.rfind("/Qt")
                while idx != -1 and ".framework" not in dep[idx:idx+50]:
                    idx = dep.rfind("/Qt", 0, idx)
                if idx == -1:
                    continue
                new_dep = "@rpath" + dep[idx:]
            else:
                # Bundled non-Qt deps (libwebp, libsharpyuv, libtiff, libpng,
                # libjpeg, ...) live in Contents/Frameworks via macdeployqt.
                libname = dep.split("/")[-1]
                new_dep = f"@rpath/{libname}"
            run([install_name_tool, "-change", dep, new_dep, str(plugin_path)],
                check=False)
        run([install_name_tool, "-add_rpath",
             "@loader_path/../../Frameworks", str(plugin_path)],
            check=False)
        return True

    # macdeployqt on Qt 6.11 also leaves absolute /opt/homebrew/... deps in
    # several other plugin dirs. Most go unnoticed because Qt falls back to
    # built-in implementations (qsvgicon, qglib networkinformation), but
    # multimedia has no fallback — when libdarwinmediaplugin fails to load,
    # QAudioDecoder silently refuses to decode MP3 and TTS is mute. Apply
    # the same redeploy pass to every plugin dir we ship.
    plugin_dirs_to_repair = (
        "imageformats",
        "multimedia",
        "networkinformation",
        "iconengines",
        "tls",
    )
    for sub in plugin_dirs_to_repair:
        sub_dir = app_bundle / "Contents" / "PlugIns" / sub
        if not sub_dir.exists():
            continue
        for plugin_path in sorted(sub_dir.glob("*.dylib")):
            _redeploy_plugin(plugin_path, Path(sub) / plugin_path.name)

    # The two blocks below repair damage that macdeployqt does specifically
    # when the bundled Qt is the *Homebrew* qtbase/qtimageformats — it
    # strips LC_ID_DYLIB from support dylibs and misses libsharpyuv. The
    # official Qt Online Installer doesn't ship these support dylibs as
    # separate Frameworks files (its image plugins link against system
    # libs or static-link), so when qt_prefix points at the official Qt
    # install we can skip both blocks entirely. We detect this via the
    # presence of a `share/qt/plugins` layout or a path component that
    # looks like Homebrew/MacPorts.
    qt_prefix_str = str(qt_prefix) if qt_prefix else ""
    homebrew_qt = (
        qt_prefix_str.startswith("/opt/homebrew/")
        or qt_prefix_str.startswith("/usr/local/")
        or "/Cellar/" in qt_prefix_str
    )
    # Probe the system library prefix that the bundled Qt was linked against.
    # On Homebrew that's /opt/homebrew/lib (Apple Silicon) or /usr/local/lib
    # (Intel); on the official installer there's no comparable system-libs
    # location so we leave it empty and skip the dylib repair entirely.
    sys_lib_dirs = []
    if homebrew_qt:
        for candidate in (Path("/opt/homebrew/lib"), Path("/usr/local/lib")):
            if candidate.exists():
                sys_lib_dirs.append(candidate)

    imageformats_dir = app_bundle / "Contents" / "PlugIns" / "imageformats"
    if homebrew_qt and imageformats_dir.exists():
        # WEBP needs libsharpyuv.0.dylib which macdeployqt misses (it tracks
        # libwebp* but not its transitive sharpyuv dependency). Locate it
        # next to libwebp.7.dylib in the same Homebrew prefix.
        sharpyuv_dst = bundle_fw / "libsharpyuv.0.dylib"
        if not sharpyuv_dst.exists():
            sharpyuv_candidates = []
            for sys_lib in sys_lib_dirs:
                # libsharpyuv ships as part of the webp keg; look both
                # directly under <prefix>/lib and one level up under opt/.
                sharpyuv_candidates.append(sys_lib / "libsharpyuv.0.dylib")
                sharpyuv_candidates.append(
                    sys_lib.parent / "opt" / "webp" / "lib" / "libsharpyuv.0.dylib"
                )
            for sharpyuv_src in sharpyuv_candidates:
                if sharpyuv_src.exists():
                    shutil.copy2(sharpyuv_src, sharpyuv_dst)
                    if codesign_path:
                        run([codesign_path, "--remove-signature", str(sharpyuv_dst)], check=False)
                    if install_name_tool:
                        run([install_name_tool, "-id", "@rpath/libsharpyuv.0.dylib",
                             str(sharpyuv_dst)], check=False)
                        run([install_name_tool, "-add_rpath", "@loader_path/.",
                             str(sharpyuv_dst)], check=False)
                    break

    # macdeployqt corrupts the bundled support dylibs in Contents/Frameworks
    # (libjpeg, libpng, libwebp, libtiff, ...) — it strips LC_ID_DYLIB so dyld
    # rejects them with "MH_DYLIB is missing LC_ID_DYLIB", which in turn makes
    # the imageformats plugins fail to load even when the plugins themselves
    # are healthy. Recopy each support dylib from the system library prefix
    # the bundled Qt links against (Homebrew only — the official Qt installer
    # doesn't ship these as separate dylibs).
    if homebrew_qt and install_name_tool and otool_path and sys_lib_dirs:
        for dylib_path in sorted(bundle_fw.glob("*.dylib")):
            src = None
            for sys_lib in sys_lib_dirs:
                candidate = sys_lib / dylib_path.name
                if candidate.exists():
                    src = candidate
                    break
            if src is None:
                # No system-lib counterpart (e.g. one of our own bundled deps);
                # skip and hope macdeployqt got that one right.
                continue
            os.chmod(dylib_path, 0o644)
            shutil.copy2(src, dylib_path)
            if codesign_path:
                run([codesign_path, "--remove-signature", str(dylib_path)], check=False)
            run([install_name_tool, "-id", f"@rpath/{dylib_path.name}", str(dylib_path)],
                check=False)
            deps = subprocess.run([otool_path, "-L", str(dylib_path)],
                                  capture_output=True, text=True, check=False).stdout
            for line in deps.splitlines()[1:]:
                line = line.strip()
                if not line:
                    continue
                dep = line.split(" (")[0]
                if not any(dep.startswith(str(d) + "/") for d in sys_lib_dirs) \
                   and not dep.startswith("/opt/homebrew/") \
                   and not dep.startswith("/usr/local/"):
                    continue
                new_dep = f"@rpath/{dep.split('/')[-1]}"
                if new_dep == f"@rpath/{dylib_path.name}":
                    continue  # self-reference; already covered by -id
                run([install_name_tool, "-change", dep, new_dep, str(dylib_path)],
                    check=False)

    # Ad-hoc sign bottom-up (allows running on modern macOS without notarization).
    # --deep alone is unreliable; sign leaf binaries explicitly first.
    codesign = shutil.which("codesign")
    if codesign:
        import glob as _glob
        # Sign frameworks and dylibs
        for pattern in [
            str(app_bundle / "Contents" / "Frameworks" / "**" / "*.dylib"),
            str(app_bundle / "Contents" / "Frameworks" / "**" / "Versions" / "A" / "*"),
            str(app_bundle / "Contents" / "PlugIns" / "**" / "*.dylib"),
        ]:
            for f in _glob.glob(pattern, recursive=True):
                if Path(f).is_file():
                    run([codesign, "--force", "--sign", "-", f], check=False)
        # Sign main binary then bundle
        run([codesign, "--force", "--sign", "-", str(app_bundle / "Contents" / "MacOS" / "Seelie")], check=False)
        run([codesign, "--deep", "--force", "--sign", "-", str(app_bundle)], check=False)

    xattr = shutil.which("xattr")
    if xattr:
        run([xattr, "-cr", str(app_bundle)], check=False)

    # Create DMG with Applications symlink for drag-to-install
    dmg_staging = build_dir / "_dmg_staging"
    if dmg_staging.exists():
        shutil.rmtree(dmg_staging)
    dmg_staging.mkdir()
    shutil.copytree(app_bundle, dmg_staging / "Seelie.app", symlinks=True)
    (dmg_staging / "Applications").symlink_to("/Applications")

    dmg_path = build_dir / f"Seelie-{version}.dmg"
    run([
        "hdiutil", "create",
        "-volname", "Seelie",
        "-srcfolder", str(dmg_staging),
        "-ov",
        "-format", "UDZO",
        str(dmg_path),
    ])
    shutil.rmtree(dmg_staging)
    print(f"\n[SUCCESS] DMG created: {dmg_path}")


def package_linux(build_dir, version, cmake, config="Release"):
    print("\n[Linux] Packaging AppImage...")

    appimagetool = find_appimagetool()
    linuxdeploy = find_linuxdeploy()

    if not appimagetool and not linuxdeploy:
        print(
            "ERROR: Neither appimagetool nor linuxdeploy found in PATH.\n"
            "Install appimagetool from https://github.com/AppImage/AppImageKit/releases\n"
            "or linuxdeploy from https://github.com/linuxdeploy/linuxdeploy/releases"
        )
        sys.exit(1)

    appdir = build_dir / "AppDir"
    if appdir.exists():
        shutil.rmtree(appdir)
    appdir.mkdir(parents=True, exist_ok=True)

    usr = appdir / "usr"
    print("  Installing to AppDir/usr...")
    run([str(cmake), "--install", str(build_dir), "--prefix", str(usr), "--config", config])

    # Create .desktop entry
    desktop_content = (
        "[Desktop Entry]\n"
        "Name=Seelie\n"
        "Comment=A native Qt6/C++ desktop pet\n"
        "Exec=Seelie\n"
        "Icon=seelie\n"
        "Type=Application\n"
        "Categories=Utility;Qt;\n"
        "Terminal=false\n"
    )
    desktop_file = appdir / "seelie.desktop"
    desktop_file.write_text(desktop_content, encoding="utf-8")
    # Also install into FHS location
    apps_dir = usr / "share" / "applications"
    apps_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(desktop_file, apps_dir / "seelie.desktop")

    # Install icon
    icon_src = PROJECT_ROOT / "assets" / "icons" / "seelie.png"
    if icon_src.exists():
        # AppDir root (appimagetool looks here)
        shutil.copy2(icon_src, appdir / "seelie.png")
        # FHS location
        icons_dir = usr / "share" / "icons" / "hicolor" / "256x256" / "apps"
        icons_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(icon_src, icons_dir / "seelie.png")
    else:
        print("  [WARNING] Icon not found at assets/icons/seelie.png")

    # AppRun symlink — cmake install always lands the binary at usr/bin/Seelie
    # via install(TARGETS Seelie RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}).
    exe = usr / "bin" / "Seelie"
    if not exe.exists():
        print(f"ERROR: Executable not found at {exe} after cmake install.")
        sys.exit(1)
    apprun = appdir / "AppRun"
    os.symlink("usr/bin/Seelie", apprun)

    arch = platform.machine().replace("_", "-")
    appimage_name = f"Seelie-{version}-{arch}.AppImage"
    appimage_path = build_dir / appimage_name

    if appimagetool:
        print(f"  [OK] appimagetool: {appimagetool}")
        run([appimagetool, str(appdir), str(appimage_path)])
    elif linuxdeploy:
        print(f"  [OK] linuxdeploy: {linuxdeploy}")
        # linuxdeploy picks the output filename itself based on .desktop
        # metadata. Snapshot the existing *.AppImage set so we can identify
        # the new one without racing on mtime against stale files from
        # earlier runs.
        before = set(build_dir.glob("*.AppImage"))
        ld_cmd = [
            linuxdeploy,
            "--appdir", str(appdir),
            "--desktop-file", str(desktop_file),
            "--output", "appimage",
        ]
        if icon_src.exists():
            ld_cmd.extend(["--icon-file", str(icon_src)])
        run(ld_cmd, cwd=build_dir)
        new_files = [p for p in build_dir.glob("*.AppImage") if p not in before]
        if new_files:
            new_files[0].rename(appimage_path)
        elif appimage_path.exists():
            pass  # linuxdeploy happened to write our exact name
        else:
            print(f"\n[WARNING] linuxdeploy produced no new *.AppImage in {build_dir}")

    print(f"\n[SUCCESS] AppImage created: {appimage_path}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def find_compiler_bin_for_qt(qt_prefix):
    """
    On Windows with MinGW/LLVM-MinGW Qt, find the matching compiler bin
    directory under Qt Tools so we can add it to PATH.
    """
    if not qt_prefix or sys.platform != "win32":
        return None
    qt_root = qt_prefix
    for _ in range(2):
        qt_root = qt_root.parent
    tools = qt_root / "Tools"
    if not tools.exists():
        return None

    qt_name = qt_prefix.name.lower()
    if "llvm-mingw" in qt_name:
        pattern = "llvm-mingw*"
    elif "mingw" in qt_name:
        pattern = "mingw*"
    elif "msvc" in qt_name:
        # MSVC compiler should come from Developer Command Prompt
        return None
    else:
        return None

    matches = sorted(tools.glob(pattern), key=lambda d: d.name, reverse=True)
    for m in matches:
        gpp = m / "bin" / "g++.exe"
        gcc = m / "bin" / "gcc.exe"
        if gpp.exists() or gcc.exists():
            return m / "bin"
    return None


def main():
    parser = argparse.ArgumentParser(description="Build and package Seelie for the current platform.")
    parser.add_argument("--build-dir", default="build", help="CMake build directory (default: build)")
    parser.add_argument("--config", default="Release", help="CMake build configuration (default: Release)")
    # `cmake --build --parallel` *without a number* falls back to the native
    # tool's default, which is "every available core". On thermally-limited
    # machines that combined with rlottie's heavy templates + 35-pack bundling
    # has caused full freezes. Default to a conservative 2; users can pass
    # --jobs N (or set CMAKE_BUILD_PARALLEL_LEVEL) to override.
    parser.add_argument("--jobs", "-j", type=int,
                        default=int(os.environ.get("CMAKE_BUILD_PARALLEL_LEVEL", "2")),
                        help="Parallel build jobs (default: 2, or $CMAKE_BUILD_PARALLEL_LEVEL)")
    parser.add_argument("--qt-dir", default=None, help="Override Qt installation directory")
    parser.add_argument("--skip-build", action="store_true", help="Skip build step (use existing binaries)")
    parser.add_argument("--skip-package", action="store_true", help="Only build, skip packaging")
    parser.add_argument("--include-nsfw", action="store_true",
                        help="Bundle NSFW pack categories (azur_lane). Default builds ship the SFW lineup only.")
    args = parser.parse_args()

    build_dir = PROJECT_ROOT / args.build_dir
    version = get_project_version()
    print(f"Project version: {version}")
    print(f"Build directory: {build_dir}")
    print(f"Platform: {sys.platform}")

    # ------------------------------------------------------------------
    # Detect Qt
    # ------------------------------------------------------------------
    print("\n[Detect] Qt environment...")
    cached_qt = get_cached_qt_prefix(build_dir) if not args.qt_dir else None
    qt_prefix = find_qt_prefix(cached_qt) if not args.qt_dir else Path(args.qt_dir)
    if args.qt_dir:
        print(f"  [OK] Qt prefix from --qt-dir: {qt_prefix}")
    elif qt_prefix:
        print(f"  Qt prefix: {qt_prefix}")
    else:
        print("  [WARNING] Could not auto-detect Qt prefix. Relying on cmake to find Qt.")

    # ------------------------------------------------------------------
    # Detect essential tools
    # ------------------------------------------------------------------
    print("\n[Detect] Essential tools...")
    cmake = find_cmake(qt_prefix)
    if cmake:
        print(f"  [OK] cmake: {cmake}")
    else:
        print("  [MISSING] cmake not found in PATH or Qt Tools (install from https://cmake.org/download/)")
        sys.exit(1)

    # Prefer Ninja, fallback to Make
    ninja = find_ninja(qt_prefix)
    make = shutil.which("make")
    if ninja:
        print(f"  [OK] ninja: {ninja}")
    elif make:
        print(f"  [OK] make: {make}")
    else:
        if sys.platform != "win32":
            print("  [WARNING] Neither ninja nor make found. CMake will pick a generator automatically.")

    # ------------------------------------------------------------------
    # Configure
    # ------------------------------------------------------------------
    print("\n[Build] Configuring...")
    configure_cmd = [
        str(cmake), "-B", str(build_dir), "-S", str(PROJECT_ROOT),
        f"-DCMAKE_BUILD_TYPE={args.config}",
    ]
    if ninja:
        configure_cmd.extend(["-GNinja", f"-DCMAKE_MAKE_PROGRAM={ninja}"])
    if qt_prefix:
        configure_cmd.append(f"-DCMAKE_PREFIX_PATH={qt_prefix}")

    # Always pin SEELIE_INCLUDE_NSFW explicitly so a prior --include-nsfw run
    # cached in CMakeCache.txt cannot silently leak into a subsequent SFW
    # release build. Flag at invocation always wins.
    nsfw_value = "ON" if args.include_nsfw else "OFF"
    configure_cmd.append(f"-DSEELIE_INCLUDE_NSFW={nsfw_value}")
    if args.include_nsfw:
        print("  Bundling NSFW pack categories (--include-nsfw)")

    # On Linux set a relocatable RPATH so the AppImage finds bundled libraries
    if sys.platform.startswith("linux"):
        configure_cmd.append("-DCMAKE_INSTALL_RPATH=$ORIGIN/../lib")

    env = os.environ.copy()
    if sys.platform == "win32":
        # For MinGW/LLVM-MinGW Qt, the matching compiler must be on PATH
        compiler_bin = find_compiler_bin_for_qt(qt_prefix)
        if compiler_bin:
            env["PATH"] = str(compiler_bin) + os.pathsep + env.get("PATH", "")
            print(f"  Prepending compiler to PATH: {compiler_bin}")
        else:
            # Warn if we detect MinGW Qt but no compiler
            if qt_prefix and ("mingw" in qt_prefix.name.lower() or "llvm-mingw" in qt_prefix.name.lower()):
                print("  [WARNING] MinGW/LLVM-MinGW Qt detected but no matching compiler found in Qt Tools.")
                print("            Make sure you open the correct Qt shell or add the compiler to PATH.")

    run(configure_cmd, env=env)

    # ------------------------------------------------------------------
    # Build
    # ------------------------------------------------------------------
    if not args.skip_build:
        print(f"\n[Build] Building ({args.config}, -j{args.jobs})...")
        build_cmd = [str(cmake), "--build", str(build_dir), "--config", args.config,
                     "--parallel", str(args.jobs)]
        run(build_cmd, env=env)
    else:
        print("\n[Build] Skipped (--skip-build)")

    if args.skip_package:
        print("\n[Package] Skipped (--skip-package)")
        return

    # ------------------------------------------------------------------
    # Package
    # ------------------------------------------------------------------
    if sys.platform == "win32":
        package_windows(build_dir, version, qt_prefix, cmake, args.config)
    elif sys.platform == "darwin":
        package_macos(build_dir, version, qt_prefix)
    elif sys.platform.startswith("linux"):
        package_linux(build_dir, version, cmake, args.config)
    else:
        print(f"ERROR: Unsupported platform: {sys.platform}")
        sys.exit(1)


if __name__ == "__main__":
    main()
