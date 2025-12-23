# foo_nowbar

A foobar2000 component that provides a "Now Playing" control panel for both Default UI (DUI) and Columns UI (CUI).

<img width="1230" height="151" alt="foo_nowbar" src="https://github.com/user-attachments/assets/6de52277-c87d-4154-8a31-98b29ee516b2" />

## Features

- **Dual UI Support**: Works with both Default UI and Columns UI
- **Album Artwork Display**: Shows album art
- **Track Information**: Displays track title and artist name
- **Playback Controls**: Shuffle, Previous, Play/Pause, Next, and Repeat buttons
- **Heart/Mood Button** - Heart icon to "like" a track
- **Seek Bar**: Interactive progress bar with elapsed and remaining time display
- **Volume Control**: Volume slider with mute toggle
- **MiniPlayer Launch**: Quick access button to launch the MiniPlayer (requires [foo_traycontrols](https://github.com/jame25/foo_traycontrols))
- **Dark/Light Mode**: Automatically adapts to foobar2000's theme
- **DPI Aware**: Properly scales on high-DPI displays

## Installation

1. Copy `foo_nowbar.dll` to your foobar2000 `components` folder:
2. Restart foobar2000
3. Add the panel:
   - **Default UI**: View → Layout → Add panel → "Nowbar Control Panel"
   - **Columns UI**: Add "Nowbar Control Panel" panel to your layout

## Building from Source

### Prerequisites

- Visual Studio 2022 (v143 toolset)
- foobar2000 SDK
- Columns UI SDK
- Windows 10 SDK

### Build Commands

**64-bit Release:**
```bash
msbuild foo_nowbar.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /v:minimal
```

**32-bit Release:**
```bash
msbuild foo_nowbar.vcxproj /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v143 /v:minimal
```

> **Note**: Ensure all SDK libraries (foobar2000_SDK, pfc, columns_ui_sdk) are built with matching runtime library settings (`/MT` for static).

## Project Structure

```
foo_nowbar/
├── core/
│   ├── control_panel_core.cpp   # Core rendering and interaction logic
│   ├── control_panel_core.h     # Core class and layout metrics
│   ├── playback_state.cpp       # Playback state tracking
│   └── playback_state.h
├── ui/
│   ├── control_panel_cui.cpp    # Columns UI wrapper
│   ├── control_panel_cui.h
│   ├── control_panel_dui.cpp    # Default UI wrapper
│   └── control_panel_dui.h
├── component.cpp                # Component initialization
├── component_client.cpp         # foobar2000 component client
├── guids.h                      # Component GUIDs
├── pch.h / pch.cpp              # Precompiled headers
└── version.h                    # Version information
```

## Controls

| Element | Action |
|---------|--------|
| **Play/Pause** | Click to toggle playback |
| **Previous/Next** | Navigate tracks |
| **Shuffle** | Toggle shuffle mode (blue when active) |
| **Repeat** | Cycle repeat modes: Off → All → Track (blue when active) |
| **Seek Bar** | Click or drag to seek within track |
| **Volume Icon** | Click to mute/unmute |
| **Volume Bar** | Click or drag to adjust volume |
| **MiniPlayer Icon** | Launch/toggle MiniPlayer (blue when active) |
| **Artwork** | Click to view in album art viewer |

## Customization

Layout metrics can be modified in `control_panel_core.h`:

```cpp
struct LayoutMetrics {
    int panel_height = 80;
    int artwork_size = 128;      // Album art size
    int button_size = 38;        // Control button size
    int play_button_size = 48;   // Play button size (larger)
    int seekbar_height = 9;      // Seek/volume bar thickness
    int volume_width = 192;      // Volume bar width
    int spacing = 16;            // Element spacing
    int text_height = 30;        // Text line height
};
```

## Dependencies

- [foobar2000 SDK](https://www.foobar2000.org/SDK)
- [Columns UI SDK](https://github.com/reupen/columns_ui-sdk)

## License

This component is provided as-is for personal use with foobar2000.
