# foo_nowbar

A foobar2000 component that provides a "Now Playing" control panel for both Default UI and Columns UI.

<img width="1434" height="108" alt="nowbar" src="https://github.com/user-attachments/assets/bd1a04b9-9a9b-49ca-85c6-7e6bd7f722ee" />

## Features

### Core Features
- **Dual UI Support**: Works seamlessly with both Default UI (DUI) and Columns UI (CUI)
- **Album Artwork Display**: Shows album art edge-to-edge (optional margin)
- **Online Artwork**: Fetches artwork from online sources via [foo_artwork](https://github.com/jame25/foo_artwork) when local/embedded artwork is unavailable
- **Track Information**: Displays track info with customizable Title Formatting (default: title and artist)
- **DPI Aware**: Properly scales on high-DPI displays with adaptive sizing

### Playback Controls
- **Play/Pause**: Central play button to toggle playback
- **Stop**: Optional stop button (hidden by default) - stops playback
- **Stop After Current**: Optional toggle button (hidden by default) - stops playback after the current track finishes; accent color when active
- **Previous/Next**: Navigate between tracks
- **Shuffle**: Toggle shuffle mode with visual indicator (accent color when active)
- **Repeat**: Cycle through repeat modes: Off → All → Track (accent color when active)
- **Seek Bar**: Interactive progress bar for seeking within tracks
  - Pill-shaped or rectangular style (configurable)
  - Elapsed and remaining time display
  - Hover tooltip showing timestamp at cursor position

### Volume Control
- **Volume Slider**: Click or drag to adjust volume (perceptual loudness mapping matching foobar2000's default curve)
- **Mute Toggle**: Click volume icon to mute/unmute
- **Mouse Wheel Support**: Scroll over panel to adjust volume
- **dB Tooltip**: Shows current volume level on mouse wheel scroll

### Special Buttons
- **Heart/Mood Button**: Toggle to "like" a track
  - Configurable tag: `%FEEDBACK%`, `%2003_LOVED%`, `%LFM_LOVED%`, `%SMP_LOVED%`, or `%MOOD%`
  - Red when mood is set, gray when empty
  - Can be hidden via preferences
- **MiniPlayer Launch**: Quick access button to launch the MiniPlayer
  - Requires [foo_traycontrols](https://github.com/jame25/foo_traycontrols)
  - Blue when active, can be hidden via preferences
- **Super Button**: Quick access menu with autoplaylist queries and playback options
  - Autoplaylist queries: Never played, Recently played, Unrated, Rated 3-5/4/5, Loved tracks, Recently added, Same artist/title as selected
  - **Infinite Playback**: Toggle to automatically add similar tracks when the playlist ends (genre-based matching from media library)
  - **Playback Preview**: Submenu to auto-skip after a portion of each track (Off / 35% / 50% / 60 seconds)
  - **Settings**: Quick link to open Now Bar preferences
- **Up to 12 Custom Buttons**: Fully configurable action buttons
  - **Buttons 1-6**: Visible on panel, configurable via Preferences or config file
  - **Buttons 7-12**: Hidden, keyboard shortcuts only (config file only)
  - Open URL, Run executable, Open folder, or Execute foobar2000 menu commands (main menu or context menu)
  - **State feedback**: Foobar2k Action buttons show accent color when the command is checked/active (e.g., toggle commands)
  - Custom PNG/ICO/SVG icon support per button (SVG requires [foo_svg_services](https://www.foobar2000.org/components/view/foo_svg_services))
  - Custom tooltip label per button
  - Adaptive layout: 2-row (buttons 1-3, 4-6) at larger heights, single row at smaller heights
  - Auto-hide during playback (optional, with smooth 300ms fade animation)
  - URL/Executable actions use selected track for title formatting (not playing track)
  - Configuration profiles for saving/loading button sets (New / Rename / Delete / Export / Import)
  - Config file location: `<foobar2000_profile>\foo_nowbar_data\custom_buttons.conf`

### Visualization Modes
- **Spectrum Visualizer**: Full-width spectrum analyzer behind playback buttons
  - Configurable bar width (Thin / Normal / Wide) and shape (Pill / Rectangle)
  - Thin progress bar at top edge replaces the seekbar; enlarges on hover for seeking
  - Time display repositioned to top-right corner
  - 30 FPS default, optional 60 FPS mode
- **Waveform Progress Bar**: SoundCloud-style pre-computed waveform replaces the seekbar
  - RMS-based computation with played/unplayed color distinction
  - Waveform data cached to disk across sessions (`wavecache.db`)
  - Full seeking support with time tooltip on hover

### Theming & Appearance
- **Theme Modes**:
  - **Auto**: Follows foobar2000's theme automatically
  - **Dark**: Forces dark mode
  - **Light**: Forces light mode
  - **Custom**: Syncs with DUI color scheme (DUI) or CUI colour manager (CUI)
- **Background Style**:
  - **Solid**: Standard solid color background
  - **Artwork Colors**: Dynamic gradient extracted from album art's dominant colors
  - **Blurred Artwork**: Album art blurred as ambient background
- **Seek/Volume Bar Style**: Pill-shaped or rectangular
- **Play Icon Style**: Normal (accent circle, dark icon) or Inverted (accent circle, white icon)
- **Hover Circles**: Optional hover effect on buttons
- **Hover Enlarge Effect**: Playback controls enlarge 15% on hover
- **Alternate Icons**: Alternative play/pause/stop icon style (outline vs filled)
- **Custom Fonts**: Select custom fonts for track title, artist, and time display
- **10 Custom Colors**: Button accent, play/pause accent, progress accent, volume accent, hover color, spectrum color, waveform played/unplayed colors, progress track color, volume track color (each independently toggleable between custom and theme-derived)
- **Smooth Animations**: Toggle for animated transitions (disabled by default for performance)
  - When enabled: custom button auto-hide fade, background crossfade
  - When disabled: instant transitions for better performance
- **Glass Effect**: Windows 11 acrylic backdrop blur behind the panel (semi-transparent background)
- **Cover Artwork**: Toggle artwork visibility; optional margin around artwork
- **Online Artwork**: Fetch artwork from online sources when local art is unavailable (requires [foo_artwork](https://github.com/jame25/foo_artwork))

### Playback Features
- **Skip Low Rating**: Automatically skip tracks with low ratings during playback (configurable threshold 1-3 stars; requires foo_playcount)
- **Infinite Playback**: Automatically add genre-matched tracks from the media library when the playlist ends
- **Playback Preview**: Auto-skip after a portion of each track for quick browsing (Off / 35% / 50% / 60 seconds)

## Installation

1. Copy `foo_nowbar.dll` to your foobar2000 `components` folder
2. Restart foobar2000
3. Add the panel:
   - **Default UI**: View → Layout → Add panel → "Now Bar Control Panel"
   - **Columns UI**: Add "Now Bar Control Panel" panel to your layout

## Preferences

Access preferences via: File → Preferences → Display → Now Bar

### General Tab

| Setting | Options | Description |
|---------|---------|-------------|
| Line 1 Format | Title Formatting | First line of track info (default: `%title%`) |
| Line 2 Format | Title Formatting | Second line of track info (default: `%artist%`) |
| Mood Tag | FEEDBACK / 2003_LOVED / LFM_LOVED / SMP_LOVED / MOOD | Which tag the heart button reads/writes |
| Skip Low Rating | Disabled / Enabled | Auto-skip low-rated tracks during playback |
| Rating Threshold | 1 / 2 / 3 | Skip tracks rated at or below this threshold |
| Visualization | Disabled / Spectrum / Waveform | Visualization mode selector |
| 60 FPS | Checkbox | Run spectrum at 60fps instead of 30fps |
| Spectrum Width | Thin / Normal / Wide | Spectrum bar count |
| Spectrum Shape | Pill-shaped / Rectangle | Spectrum bar shape |
| Waveform Width | Thin / Normal / Wide | Waveform bar density |

### Appearance Tab

| Setting | Options | Description |
|---------|---------|-------------|
| Theme Mode | Auto / Dark / Light / Custom | Controls panel color scheme |
| Cover Artwork | Yes / No | Show or hide album artwork |
| Cover Margin | Yes / No | Add margin around artwork |
| Background Style | Solid / Artwork Colors / Blurred Artwork | Panel background appearance |
| Bar Style | Pill-shaped / Rectangular | Seek and volume bar shape |
| Glass Effect (Win11) | Disabled / Enabled | Windows 11 acrylic backdrop blur effect |
| Smooth Animations | Enabled / Disabled | Smooth animated transitions |
| Online Artwork | Checkbox | Fetch online artwork (requires foo_artwork) |

### Icons Tab

| Setting | Options | Description |
|---------|---------|-------------|
| Mood Icon | Show / Hidden | Toggle heart button visibility |
| Stop Button | Show / Hidden | Toggle stop button visibility |
| Stop After Current | Show / Hidden | Toggle stop-after-current button visibility |
| Super Button | Show / Hidden | Toggle Super menu button visibility |
| MiniPlayer Icon | Show / Hidden | Toggle MiniPlayer button visibility |
| Hover Circles | Show / Hide | Show circular hover effect on buttons |
| Alternate Icons | Enabled / Disabled | Use outline-style play/pause/stop icons |
| Play Icon Style | Normal / Inverted | Normal: accent bg + dark icon; Inverted: accent bg + white icon |
| Auto-hide C-buttons | Yes / No | Custom buttons fade out during active playback |

### Custom Button Tab

#### Configuration Profile
- **Configuration name**: Select between saved button configurations
- **[...] menu**: New, Rename, Delete, Export, Import options for profiles

#### Button Configuration
Configure up to 6 visible custom buttons (7-12 via config file only):
- **None**: Button disabled
- **Open URL**: Opens a URL in the default browser (supports Title Formatting, uses selected track)
- **Run Executable**: Launches an external program (supports Title Formatting, uses selected track)
- **Foobar2k Action**: Executes a foobar2000 menu command (e.g., `Playback/Stop` or context menu `File Operations/Move to/MP3`); shows accent color when the command is active
- **Open Folder**: Opens the directory containing the currently selected track

Each button also supports:
- **Custom Icon (PNG/ICO/SVG)**: Optional custom icon image path (SVG requires [foo_svg_services](https://www.foobar2000.org/components/view/foo_svg_services))
- **Tooltip Label**: Custom text shown on hover (defaults to "Button #N")
- Automatic fallback to numbered square icons when custom icon is missing

### Fonts & Colors Tab

#### Fonts
- **Track Title**: Font for the track name (Line 1)
- **Artist**: Font for the artist/second line (Line 2)
- **Time Display**: Font for elapsed/remaining time

#### Colors
Each color has a checkbox: unchecked uses theme colors from DUI/CUI, checked uses a custom color.

| Color | Default (theme) | Description |
|-------|----------------|-------------|
| Button Accent | DUI Highlight / CUI highlight | Active state for Shuffle, Repeat, etc. |
| Play/Pause Accent | DUI Highlight / CUI highlight | Play button background circle |
| Progress Accent | DUI Highlight / CUI highlight | Progress bar fill |
| Volume Accent | DUI Highlight / CUI highlight | Volume bar fill |
| Hover Color | DUI Selection / CUI selection | Button hover circle |
| Spectrum Color | DUI Highlight / CUI highlight | Spectrum visualizer bars |
| Waveform Color | DUI Highlight / CUI highlight | Waveform played portion |
| Waveform Unplayed | DUI Text / CUI text | Waveform unplayed portion |
| Progress Track | Theme-derived | Progress bar unfilled portion |
| Volume Track | Theme-derived | Volume bar unfilled portion |

### Keyboard Shortcuts
All 12 custom button actions can be triggered via keyboard shortcuts:

1. Go to **Preferences → Keyboard Shortcuts**
2. Find commands under **File → Now Bar**:
   - `Custom Button 1` through `Custom Button 12`
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
├── artwork_bridge.cpp           # Runtime bridge to foo_artwork for online artwork
├── artwork_bridge.h
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
| **Play/Pause** | Click to toggle playback |
| **Stop** | Stop playback (optional, hidden by default) |
| **Stop After Current** | Toggle stop-after-current (optional, hidden by default) |
| **Previous/Next** | Navigate tracks |
| **Shuffle** | Toggle shuffle mode (accent color when active) |
| **Repeat** | Cycle repeat modes: Off → All → Track (accent color when active) |
| **Super Button** | Open menu: autoplaylists, infinite playback, preview, settings |
| **Seek Bar** | Click or drag to seek within track (tooltip shows timestamp) |
| **Volume Icon** | Click to mute/unmute |
| **Volume Bar** | Click or drag to adjust volume |
| **Mouse Wheel** | Scroll over panel to adjust volume (tooltip shows dB) |
| **Heart Icon** | Toggle mood tag for current track (red when set) |
| **MiniPlayer Icon** | Launch/toggle MiniPlayer (blue when active) |
| **Custom Buttons** | Execute configured action; accent color when toggled on |
| **Artwork** | Double-click to view in album art viewer |
| **Track Info** | Double-click to highlight playing track in playlist |

## Dependencies

- [foobar2000 SDK](https://www.foobar2000.org/SDK)
- [Columns UI SDK](https://github.com/reupen/columns_ui-sdk)
- [foo_traycontrols](https://github.com/jame25/foo_traycontrols) (optional, for MiniPlayer functionality)
- [foo_artwork](https://github.com/jame25/foo_artwork) (optional, for online artwork)
- [foo_svg_services](https://www.foobar2000.org/components/view/foo_svg_services) (optional, for SVG custom button icons)

## License

This component is provided as-is for personal use with foobar2000.

## Support Development

If you find these components useful, consider supporting development:

| Platform | Payment Methods |
|----------|----------------|
| **[Ko-fi](https://ko-fi.com/Jame25)** | Cards, PayPal |
| **[Stripe](https://buy.stripe.com/3cIdR874Bg1NfRdaJf1sQ02)** | Alipay, Cards, Apple Pay, Google Pay |

Your support helps cover development time and enables new features. Thank you! 🙏

---

## 支持开发

如果您觉得这些组件有用，请考虑支持开发：

| 平台 | 支付方式 |
|------|----------|
| **[Ko-fi](https://ko-fi.com/Jame25)** | 银行卡、PayPal |
| **[Stripe](https://buy.stripe.com/dRmcN474B8zlfRd2cJ1sQ01)** | 支付宝、银行卡、Apple Pay、Google Pay |

您的支持有助于支付开发时间并实现新功能。谢谢！🙏

---

**Feature Requests:** Paid feature requests are available for supporters. [Contact me on Telegram](https://t.me/j4m31) to discuss.

**功能请求：** 为支持者提供付费功能请求。[请在 Telegram 上联系我](https://t.me/j4m31) 进行讨论。
