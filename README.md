# Dead or Alive 4 — PC recomp

A native Windows version of **Dead or Alive 4** (Xbox 360), made by recompiling the game's code ahead of
time with the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk).

This repository contains **no game data**. You build the program yourself (this guide walks you through
it), then run it with the files from your own copy of the game.

**Contents**

1. [Install the tools](#1-install-the-tools)
2. [Get your game files (Rev 1 only)](#2-get-your-game-files-rev-1-only)
3. [Download the source code](#3-download-the-source-code)
4. [Build](#4-build)
5. [Play](#5-play)
6. [The Esc overlay](#6-the-esc-overlay)
7. [If something goes wrong](#7-if-something-goes-wrong)
8. [For developers](#8-for-developers)

You need a **64-bit Windows 10 or 11 PC** with a processor from roughly 2015 or newer (it must support
AVX2: Intel 4th generation "Haswell" or later, AMD Ryzen or later), about **25 GB of free disk space**,
and an internet connection for the downloads. The first build takes a while (often 20-60 minutes); after
that you only build again when you update.

---

## 1. Install the tools

Install these four programs, in this order. Each one is free. When an installer offers options not
mentioned here, the defaults are fine.

| # | Tool | What it is for | Download |
|---|---|---|---|
| 1 | **Git** | Downloads the source code | <https://git-scm.com/download/win> |
| 2 | **Build Tools for Visual Studio** | Microsoft's C++ build tools (also brings CMake and Ninja) | <https://visualstudio.microsoft.com/downloads/> |
| 3 | **LLVM** | The compiler this project uses | <https://github.com/llvm/llvm-project/releases> |
| 4 | **Python 3** | Runs two small helper scripts | <https://www.python.org/downloads/windows/> |

**1. Git.** Download "64-bit Git for Windows Setup", run it and click *Next* through every page.

**2. Build Tools for Visual Studio.** On the download page, scroll down to **"Tools for Visual Studio"**
and download **"Build Tools for Visual Studio"** (you do not need the full Visual Studio). Run it; when the
installer shows a list of *Workloads*, tick **"Desktop development with C++"** and click *Install*. This
is a large download. (This project was built with the 2026 version; the 2022 version should also work.)

**3. LLVM.** On the releases page, open the newest release, expand *Assets* and download the file named
like **`LLVM-<version>-win64.exe`**. Run it; on the page about the system PATH, choose
**"Add LLVM to the system PATH for all users"** (or for the current user). Keep the default install folder
(`C:\Program Files\LLVM`).

**4. Python 3.** Download the latest "Windows installer (64-bit)". On the first page of the installer,
tick **"Add python.exe to PATH"**, then click *Install Now*.

Restart your PC after installing all four, so every program is found.

> Prefer the command line? In a terminal: `winget install Git.Git`, `winget install LLVM.LLVM`,
> `winget install Python.Python.3.14`, and install the Build Tools as above.

---

## 2. Get your game files (Rev 1 only)

> **Use the Rev 1 version of the game.** The recompiled code matches one exact version of the game:
> **Dead or Alive 4 (USA, Europe) (Rev 1)**. Other versions and other ROMs have been reported **not to
> work**.

| | |
|---|---|
| Disc image (Redump name) | `Dead or Alive 4 (USA, Europe) (En,Ja,Fr,De,Es,It,Zh,Ko) (Rev 1).iso` |
| Title ID | `544307D1` |
| `default.xex` SHA-1 | `af8011945037b37c98fb8a78d5a4e844dd56c33f` |
| `default.xex` MD5 | `b2eb9c5df1f2060ae9c616945450289f` |

Make the disc image from a disc you own.

**Create the folders.** This guide uses one folder, `C:\doa4`, for everything. Keep the path short, with
no spaces or special characters:

```
C:\doa4\
  doa4-recomp\     this project (step 3)
  rexglue-sdk\     the SDK (step 3)
  xex\DOA4\        your extracted game files (this step)
  userdata\        your saves (created when you first play)
```

**Extract the game files.** The game is not run from the `.iso` file itself; its files are extracted into
`C:\doa4\xex\DOA4`. Xbox 360 discs use their own file system, which normal zip/ISO tools cannot open, so
this project includes a small extractor. You can run it after step 3 (it lives in the project's `tools`
folder). Put your `.iso` in `C:\doa4`, open a **Command Prompt** (press the Windows key, type `cmd`, press
Enter) and type:

```bat
cd /d C:\doa4
py -3 doa4-recomp\tools\xdvdfs_extract.py "Dead or Alive 4 (USA, Europe) (En,Ja,Fr,De,Es,It,Zh,Ko) (Rev 1).iso" xex\DOA4
```

(Use your file's actual name. It extracts about 6.8 GB.) Then check that you have the right version:

```bat
certutil -hashfile C:\doa4\xex\DOA4\default.xex SHA1
```

The result must be `af8011945037b37c98fb8a78d5a4e844dd56c33f`. If it is different, you have another
version of the game and the build will not run it. The `.iso` is not needed after this.

---

## 3. Download the source code

In a Command Prompt, copy and paste these lines one at a time:

```bat
cd /d C:\doa4
git clone https://github.com/parkerallan/doa4-recomp doa4-recomp
git clone https://github.com/rexglue/rexglue-sdk rexglue-sdk
cd rexglue-sdk
git checkout e8ce24f
git submodule update --init --recursive
cd ..
```

`e8ce24f` is the exact SDK version (ReXGlue v0.8.0) this project's code was made with. Do not use a newer
SDK version.

(Now is a good time to extract your game files if you skipped that in step 2.)

---

## 4. Build

Open `C:\doa4\doa4-recomp` in File Explorer and **double-click `build.bat`**.

A black window opens and shows the progress. It finds the tools by itself, fixes a few files in the SDK
download, then builds. The first build takes a long time; your PC may be busy and loud meanwhile, which
is normal. When it finishes you see:

```
=== Done. To play, open out\build\win-amd64-release and double-click run_doa4.bat
```

Press any key to close the window. If it says something else, see
[If something goes wrong](#7-if-something-goes-wrong).

After updating the code later (`git pull` in `C:\doa4\doa4-recomp`), double-click `build.bat` again; it
only rebuilds what changed.

---

## 5. Play

Open `C:\doa4\doa4-recomp\out\build\win-amd64-release` and **double-click `run_doa4.bat`**. You can
right-click it and choose *Send to > Desktop (create shortcut)* to start the game from your desktop.

- With the folder layout from step 2 it works as it is. If your game files are somewhere else, open
  `run_doa4.bat` in Notepad and change the `GAME` line to your folder.
- Saves go to `C:\doa4\userdata`. Keep it out of OneDrive if you can (OneDrive can lock files the game is
  writing).
- The first matches can stutter briefly while the game prepares its graphics; it gets smoother as you play.
- **Esc** opens the overlay menu (section 6). **F6** shows the diagnostics overlay.
- Settings are saved in `dead_or_alive_4.toml` next to the program; the Esc menu edits it for you.

---

## 6. The Esc overlay

Press **Esc** in game to open the overlay menu, and Esc again (or *Close*) to go back. While it is open,
the game ignores your controller, so nothing you press in the menu happens in the game.

**Video tab**
- **Display mode:** Windowed or Borderless fullscreen.
- **Render scale:** draw the game at a higher resolution for a sharper picture (*Restart now* applies it).
- **Anti-aliasing** and **Texture filtering:** smoother edges and sharper textures, applied immediately.
- **Show diagnostics overlay (F6):** swap, draw, audio and scheduler figures; off at every start.

**Extras tab**
- **Unlocks:** unlock all characters, all costumes, all stages and all system voices, or everything at
  once. The first time you unlock anything, a backup of your save is made first (its location is shown
  in the tab).
- **Game tweaks:** *All stages in Tag battle*, and *Unlock Helena's opera costume* (her opera dress from
  the cutscenes as her 6th costume; turn it on, then unlock her costumes).

---

## 7. If something goes wrong

| What you see | What to do |
|---|---|
| "Could not find Visual Studio's C++ build tools" | Run the Build Tools installer again, choose *Modify*, and tick "Desktop development with C++". |
| "Could not find LLVM (clang)" | Reinstall LLVM and choose "Add LLVM to the system PATH". Restart the PC. |
| "Could not find Python" | Reinstall Python with "Add python.exe to PATH" ticked. Restart the PC. |
| "Could not find the ReXGlue SDK" | The `rexglue-sdk` folder must be next to `doa4-recomp` inside `C:\doa4` (step 3). |
| Errors mentioning `libmspack` or `o1heap` | Double-click `build.bat` again; it repairs those files first. |
| Errors mentioning `_mm_` or AVX | Delete the folder `C:\doa4\doa4-recomp\out` and double-click `build.bat` again. |
| `run_doa4.bat` says it cannot find `default.xex` | Your game files are not in `C:\doa4\xex\DOA4`; set `GAME` in `run_doa4.bat` (section 5). |
| The game closes or does not start | Check that you used the **Rev 1** game files (section 2). The newest file in `out\build\win-amd64-release\logs` says what happened. |
| `no XDVDFS volume found` from the extractor | The file is not an Xbox 360 disc image, or it is damaged. |

---

## 8. For developers

- **Manual build** (what `build.bat` does), from a *Developer Command Prompt*, with LLVM's `bin` folder
  first on `PATH`:

  ```bat
  py -3 tools\fix_git_symlinks.py ..\rexglue-sdk
  cmake --preset win-amd64-release -DREXSDK_DIR=C:/doa4/rexglue-sdk "-DCMAKE_C_FLAGS=-march=x86-64-v3" "-DCMAKE_CXX_FLAGS=-march=x86-64-v3"
  cmake --build --preset win-amd64-release
  ```

  The `-march=x86-64-v3` flags are required (the SDK uses AVX intrinsics). `fix_git_symlinks.py` replaces
  git symlink stubs in two SDK submodules with real copies (needed on Windows without Developer Mode).
- **Regenerating the recompiled code** (only after adding a function address to `hints.toml`, e.g. for a
  log line `Call to invalid or unregistered function at guest address 0x...`):
  `cmake --build --preset win-amd64-release --target dead_or_alive_4_codegen`, configure again, build.
  The generator reads `../xex/DOA4/default.xex`. `commands.txt` has more notes.
- **Host tests:** the overlay's modules have small tests that build with `clang-cl` without CMake or the
  game: run any `tests\host\run_<name>_test.bat`; each prints `<name>: all checks passed`.

---

## Credits

- Recompilation project by Parker Allan, built on the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk).
- Dead or Alive 4 and its assets are the property of Koei Tecmo. This project contains no game data.

## AI disclosure

AI tools were used to help write the code and documentation of this project.
