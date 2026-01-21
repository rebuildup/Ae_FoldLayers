# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**FoldLayers** is an AEGP (After Effects General Plugin) that recreates GM FoldLayers functionality - adding Photoshop-like layer grouping and folding to After Effects. The plugin creates group dividers in the timeline, supports hierarchical nesting, and folds/unfolds groups using layer Shy flags.

## Build Commands

### Windows (Visual Studio 2022)

```powershell
# Open solution
.\Win\FoldLayers.sln

# Build from command line
msbuild Win\FoldLayers.sln /p:Configuration=Release /p:Platform=x64

# Output location
# $(AE_PLUGIN_BUILD_DIR)\AEGP\FoldLayers.aex
```

The project uses the `AE_PLUGIN_BUILD_DIR` environment variable to determine output location. Configure this in Visual Studio project settings or set it as an environment variable.

### macOS (Xcode)

```bash
# Build using xcodebuild
xcodebuild -project Mac/FoldLayers.xcodeproj -scheme FoldLayers build

# PiPL resource generation (used as build phase)
./Mac/build_pipl.sh <plugin_dir> <plugin_path> <sdk_root>
```

Output: `FoldLayers.plugin` bundle

### SDK Path Requirements

The build system expects the After Effects SDK at a relative path:
```
../../../../../Headers/     # Main SDK headers
../../../../../Headers/SP/  # Suite-specific headers
../../../../../Headers/Win/ # Windows-specific headers (Windows only)
../../../../../Resources/   # SDK resources (PiPLTool, AE_General.r)
../../../../../Util/        # Utility classes (AEGP_SuiteHandler)
```

If your SDK is located elsewhere, update the `AdditionalIncludeDirectories` in the project file.

## Architecture

### Core Components

- **`FoldLayers.cpp`** (~1500 lines) - Main plugin implementation containing:
  - `EntryPointFunc`: Plugin entry point for initialization and command registration
  - Menu commands: Create Divider, Fold/Unfold
  - Group hierarchy parsing and layer name manipulation
  - Shy flag management for fold/unfold behavior
  - Platform-specific double-click detection (Windows) or idle hook processing

- **`FoldLayers.h`** - Core definitions:
  - Version constants: `FOLDLAYERS_MAJOR_VERSION`, `FOLDLAYERS_MINOR_VERSION`, `FOLDLAYERS_BUG_VERSION`
  - Group hierarchy constants: `MAX_HIERARCHY_DEPTH`, `GROUP_MARKER_START`, `GROUP_MARKER_END`, `GROUP_HIERARCHY_SEP`
  - Unicode prefix characters for fold state: `PREFIX_FOLDED` ("▸ "), `PREFIX_UNFOLDED` ("▾ ")
  - `FoldGroupInfo` struct for tracking group hierarchy

- **`FoldLayers_PiPL.r`** - Plugin resource definition defining metadata (name, category, version, entry point)

- **`FoldLayers_Strings.cpp/h`** - String table for internationalization

### Group Hierarchy System

Groups use a naming convention with hierarchy markers:
- Top level: `▾ Group Divider`
- Nested: `▾(1) Group Divider`
- Deeper: `▾(1/A) Group Divider`

The hierarchy is encoded as:
- Top level: No prefix (e.g., "1", "2", "3")
- First nesting: Letters A-Z (e.g., "1/A", "1/B")
- Deeper nesting: Letters continue (e.g., "1/A/i")

Max depth: 26 levels (a-z for sub-levels, defined by `MAX_HIERARCHY_DEPTH`)

### Fold State Management

Folding/unfolding is achieved through:
1. Layer Shy flags - Child layers have Shy flag set when folded
2. Composition `hideShyLayers` setting - Controls Shy layer visibility
3. Unicode prefix in layer name indicates fold state (▸ = folded, ▾ = unfolded)

### Platform-Specific Code

**Windows (`AE_OS_WIN`):**
- Mouse hook (`SetWindowsHookEx`) for double-click detection on dividers
- Critical section for thread safety
- Warnings treated as errors (`TreatWarningAsError=true`)

**macOS (`AE_OS_MAC`):**
- Uses pthread, CoreFoundation, ApplicationServices frameworks
- Idle hook for deferred processing

Platform isolation is handled via `#ifdef AE_OS_WIN` and `#ifdef AE_OS_MAC` preprocessor directives.

## Development Notes

### After Effects SDK Patterns

- **AEGP_SuiteHandler**: Centralized access to After Effects API suites
- **Error handling**: Uses `ERR()` macro and `A_Err` return codes
- **Memory management**: AEGP_MemHandle with lock/unlock pattern
- **UTF-16 strings**: Layer names are UTF-16; conversion helpers provided in `FoldLayers.cpp`

### Entry Point Flow

```
EntryPointFunc (called by AE)
  └──> AEGP_RegisterMenuCommand (Create Divider)
  └──> AEGP_RegisterMenuCommand (Fold/Unfold)
  └──> AEGP_RegisterHook (Idle hook / Windows mouse hook)
```

### Key Functions to Modify

When extending functionality:
- `BuildDividerName()` - Constructs layer names with hierarchy
- `CreateDividerCommand()` - Creates new group dividers
- `FoldUnfoldCommand()` - Toggles fold state
- `ProcessIdle()` - Background processing (especially on macOS)

## Version Bumping

Update version constants in `FoldLayers.h`:
```cpp
#define FOLDLAYERS_MAJOR_VERSION 1
#define FOLDLAYERS_MINOR_VERSION 0
#define FOLDLAYERS_BUG_VERSION 0
```

Then update the PiPL resource in `FoldLayers_PiPL.r` to match.

## Reference Documentation

See `docs/chat.md` for the original analysis of GM FoldLayers and implementation notes. The reference implementation in `docs/reference/Ae_MultiSlicer/` demonstrates AEGP patterns.
