# foo_nowbar

A foobar2000 component that provides a "Now Playing" control panel for both Default UI and Columns UI.

<img width="1430" height="107" alt="nowbar" src="https://github.com/user-attachments/assets/97373332-c90e-4809-a52e-62f8c5a1bc9e" />

## Features

### Core Features
- **Dual UI Support**: Works seamlessly with both Default UI (DUI) and Columns UI (CUI)
- **Album Artwork Display**: Shows album art with configurable margin/edge-to-edge display
- **Track Information**: Displays track info with customizable Title Formatting (default: title and artist)
- **DPI Aware**: Properly scales on high-DPI displays with adaptive sizing

### Playback Controls
- **Play/Pause**: Central play button to toggle playback
- **Stop**: Optional stop button (hidden by default) - stops playback
- **Previous/Next**: Navigate between tracks
- **Shuffle**: Toggle shuffle mode with visual indicator (blue when active)
- **Repeat**: Cycle through repeat modes: Off → All → Track (blue when active)
- **Seek Bar**: Interactive progress bar for seeking within tracks
  - Pill-shaped or rectangular style (configurable)
  - Elapsed and remaining time display

### Volume Control
- **Volume Slider**: Click or drag to adjust volume
- **Mute Toggle**: Click volume icon to mute/unmute
- **Mouse Wheel Support**: Scroll to adjust volume

### Special Buttons
- **Heart/Mood Button**: Toggle to "like" a track (modifies MOOD tag)
  - Red when mood is set, gray when empty
  - Can be hidden via preferences
- **MiniPlayer Launch**: Quick access button to launch the MiniPlayer
  - Requires [foo_traycontrols](https://github.com/jame25/foo_traycontrols)
  - Blue when active, can be hidden via preferences
- **Super Button**: Quick access to autoplaylists via popup menu
  - Preset autoplaylist queries: Never played, Recently played, Unrated, Rated 3-5/4/5, Loved tracks, Recently added, Same artist/title as playing
- **Up to 6 Custom Buttons**: Fully configurable action buttons
  - Open URL, Run executable, or Execute foobar2000 main menu commands
  - Custom PNG/ICO icon support per button
  - Adaptive layout: 2-row (buttons 1-3, 4-6) at larger heights, single row at smaller heights
  - Auto-hide during playback (optional, with smooth 300ms fade animation)

### Theming & Appearance
- **Theme Modes**:
  - **Auto**: Follows foobar2000's theme automatically
  - **Dark**: Forces dark mode
  - **Light**: Forces light mode
  - **Custom**: Syncs with DUI color scheme (DUI only)
- **Cover Margin**: Enable/disable margin around album artwork
- **Seek/Volume Bar Style**: Pill-shaped or rectangular
- **Hover Circles**: Optional hover effect on buttons
- **Hover Enlarge Effect**: Playback controls enlarge 15% on hover
- **Alternate Icons**: Alternative play/pause/stop icon style (outline vs filled)
- **Custom Fonts**: Select custom fonts for track title and artist
- **Glass Effect**: Windows 11 acrylic backdrop blur behind the panel (semi-transparent background)
- **Background Style**:
- **Solid**: Standard solid color background
- **Artwork Colors**: Dynamic gradient extracted from album art's dominant colors
- **Blurred Artwork**: Album art blurred as ambient background

## Installation

1. Copy `foo_nowbar.dll` to your foobar2000 `components` folder
2. Restart foobar2000
3. Add the panel:
   - **Default UI**: View → Layout → Add panel → "Now Bar Control Panel"
   - **Columns UI**: Add "Now Bar Control Panel" panel to your layout

## Preferences

Access preferences via: File → Preferences → Display → Now Bar

### General Tab

#### Display Format
Customize the two lines of track information using foobar2000's [Title Formatting](https://wiki.hydrogenaud.io/index.php?title=Foobar2000:Title_Formatting_Reference) syntax:

| Setting | Default | Description |
|---------|---------|-------------|
| Line 1 | `%title%` | First line of track info (e.g., `%title% · %artist%`) |
| Line 2 | `%artist%` | Second line of track info (e.g., `%codec% \| %bitrate% kbps`) |

#### Appearance Settings
| Setting | Options | Description |
|---------|---------|-------------|
| Theme Mode | Auto / Dark / Light / Custom | Controls panel color scheme |
| Cover Margin | Yes / No | Enable margin around album art |
| Seek/Volume Bar Style | Pill-shaped / Rectangular | Bar appearance |
| Mood Icon | Show / Hidden | Toggle heart button visibility |
| MiniPlayer Icon | Show / Hidden | Toggle MiniPlayer button visibility |
| Hover Circles | Yes / No | Show circular hover effect on buttons |
| Alternate Icons | Enabled / Disabled | Use outline-style play/pause/stop icons |
| Auto-hide C-buttons | Yes / No | Custom buttons fade out during active playback |
| Glass Effect (Win11) | Disabled / Enabled | Windows 11 acrylic backdrop blur effect |
| Background Style | Solid / Artwork Colors / Blurred Artwork | Panel background appearance |

### Custom Button Tab
Configure up to 6 custom buttons with the following actions:
- **None**: Button disabled
- **Open URL**: Opens a URL in the default browser (supports Title Formatting)
- **Run Executable**: Launches an external program (supports Title Formatting)
- **Foobar2k Action**: Executes a foobar2000 main menu command (e.g., `Playback/Stop`)

Each button also supports:
- **Custom Icon (PNG/ICO)**: Optional custom icon image path
- Automatic fallback to numbered square icons when custom icon is missing

### Fonts Tab
- Select custom fonts for track title and artist display
- Reset to default fonts option

### Keyboard Shortcuts
Custom button actions can be triggered via keyboard shortcuts:

1. Go to **Preferences → Keyboard Shortcuts**
2. Find commands under **Playback → Now Bar**:
   - `Custom Button 1` through `Custom Button 6`
3. Assign any keyboard shortcut to trigger the corresponding button's action

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
│   ├── control_panel_core.h     # Core class, layout metrics, and hit regions
│   ├── playback_state.cpp       # Playback state tracking and callbacks
│   └── playback_state.h         # Playback state structures
├── ui/
│   ├── control_panel_cui.cpp    # Columns UI wrapper
│   ├── control_panel_cui.h
│   ├── control_panel_dui.cpp    # Default UI wrapper
│   └── control_panel_dui.h
├── preferences.cpp              # Preferences page implementation
├── preferences.h                # Preferences declarations
├── preferences.rc               # Preferences dialog resources
├── resource.h                   # Resource IDs
├── component.cpp                # Component initialization
├── component_client.cpp         # foobar2000 component client
├── mainmenu_commands.cpp        # Main menu commands for keyboard shortcuts
├── guids.h                      # Component GUIDs
├── pch.h / pch.cpp              # Precompiled headers
└── version.h                    # Version information
```

## Controls Reference

| Element | Action |
|---------|--------|
| **Play/Pause** | Click to toggle playback; hover for 1+ second to show stop icon |
| **Previous/Next** | Navigate tracks |
| **Shuffle** | Toggle shuffle mode (blue when active) |
| **Repeat** | Cycle repeat modes: Off → All → Track (blue when active) |
| **Super Button** | Open autoplaylist menu with preset queries |
| **Seek Bar** | Click or drag to seek within track |
| **Volume Icon** | Click to mute/unmute |
| **Volume Bar** | Click or drag to adjust volume |
| **Mouse Wheel** | Scroll over panel to adjust volume |
| **Heart Icon** | Toggle MOOD tag for current track (red when set) |
| **MiniPlayer Icon** | Launch/toggle MiniPlayer (blue when active) |
| **Custom Buttons** | Execute configured action (URL / Executable / foobar2000 command) |
| **Artwork** | Click to view in album art viewer |

## Customization

Layout metrics can be modified in `control_panel_core.h`:

```cpp
struct LayoutMetrics {
    int panel_height = 80;
    int artwork_size = 128;      // Maximum album art size
    int artwork_margin = 8;      // Margin around artwork
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
- [foo_traycontrols](https://github.com/jame25/foo_traycontrols) (optional, for MiniPlayer functionality)

## License

This component is provided as-is for personal use with foobar2000.
