# FoldLayers

After Effects AEGP plugin that adds Photoshop-like layer grouping and folding functionality to the timeline.

## Features

- **Group Layers** - Create group divider layers to organize your timeline
- **Hierarchical Nesting** - Support up to 26 levels of nested groups (A-Z)
- **Fold/Unfold** - Toggle group visibility using layer Shy flags
- **Double-Click to Fold** - Quick fold/unfold by double-clicking group layers (Windows)
- **Visual Indicators** - Unicode prefixes (▸/▾) show fold state in layer names

## Tested Environment

- **Windows** - After Effects 2026
- Built with AE SDK 2025

## Requirements (for building)

- Windows: Visual Studio 2022
- macOS: Xcode
- After Effects SDK (placed at relative path `../../../../../`)

## Booth (Japanese Distribution)

https://361do.booth.pm/items/7935112

## Installation

### Windows

1. Build the solution: `Win\FoldLayers.sln`
2. Copy `FoldLayers.aex` to your After Effects plugins folder:
   - `C:\Program Files\Adobe\Adobe After Effects [version]\Plug-ins\Effect`

### macOS

1. Build using xcodebuild
2. Copy `FoldLayers.plugin` bundle to:
   - `/Applications/Adobe After Effects [version]/Plug-ins/Effect`

## Usage

### Creating Group Layers

1. Select one or more layers
2. Run: `Layer > Create Group Layer`
3. A new group layer is created above the selection

### Folding/Unfolding Groups

**Method 1: Double-Click (Windows)**
- Double-click a group layer to toggle fold/unfold

**Method 2: Menu Command**
1. Select a group layer
2. Run: `Layer > Fold/Unfold`
3. If no group layer is selected, all groups will be toggled

**How Folding Works**
- When a group is folded, all layers between that group layer and the next group layer become hidden (Shy)
- The group layer itself shows a Unicode prefix to indicate its state:
  - `▾` = Unfolded (children visible)
  - `▸` = Folded (children hidden)

### Creating Nested Groups

1. Select an existing group layer
2. Run: `Layer > Create Group Layer`
3. A child group is created under the selected parent group

### Group Hierarchy

Groups are named with hierarchy markers:
- Top level: `▾ Group Name`
- First nesting: `▾(A) Sub Group`
- Deeper nesting: `▾(A/i) Deep Group`

Maximum depth: 26 levels (using letters A-Z for sub-levels)

## Building

### Windows (Visual Studio 2022)

```powershell
msbuild Win\FoldLayers.sln /p:Configuration=Release /p:Platform=x64
```

Output location is controlled by `AE_PLUGIN_BUILD_DIR` environment variable.

### macOS (Xcode)

```bash
xcodebuild -project Mac/FoldLayers.xcodeproj -scheme FoldLayers build
```

## Project Structure

```
FoldLayers/
├── FoldLayers.cpp           # Main plugin implementation
├── FoldLayers.h             # Core definitions and constants
├── FoldLayers_PiPL.r        # Plugin resource definition
├── FoldLayers_Strings.cpp/h # String table for i18n
├── Win/                     # Windows project files
└── Mac/                     # macOS project files
```

## Terms of Use

- **Commercial Use** - Allowed
- **Credit Required** - No
- **License Expiration** - None

## Disclaimer

The author is not responsible for any damages caused by the use of this plugin.

## License

MIT License - see [LICENSE](LICENSE) for details.

## GitHub Repository

https://github.com/rebuildup/Ae_FoldLayers

## Contact

- **X (Twitter)**: https://x.com/361do_sleep

## Version History

- **2026.02.01** - Initial release

## Credits

Inspired by the original GM FoldLayers plugin for After Effects.

## Reference Documentation

See `docs/chat.md` for implementation notes and analysis of the original GM FoldLayers functionality.
