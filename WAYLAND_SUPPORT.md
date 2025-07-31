# Wayland Support for electron-overlay-window

This document describes the implementation of Wayland support for the electron-overlay-window library.

## Overview

The library now supports Wayland, specifically targeting KDE Plasma environments. This implementation provides experimental support for overlay windows on Wayland displays.

## Implementation Details

### Environment Detection

The library automatically detects Wayland environments by checking:
- `WAYLAND_DISPLAY` environment variable
- `XDG_SESSION_TYPE` environment variable (must be "wayland")

### Backend Selection

The library uses different backends based on the detected environment:
- **X11**: Traditional X11 window management (existing implementation)
- **Wayland**: New Wayland implementation using KDE Plasma protocols

### Wayland Implementation

#### Dependencies
- `wayland-client`: Core Wayland client library
- `xkbcommon`: XKB keyboard handling
- KDE Plasma window management protocols

#### Key Features
- Window tracking via KDE Plasma window management protocol
- Fullscreen state detection
- Window geometry tracking
- Focus/blur event handling
- Window lifecycle management

#### Protocol Support
The implementation supports the following KDE Plasma protocols:
- `org_kde_plasma_window_management`: Window management interface
- `org_kde_plasma_window`: Individual window interface

### Event Handling

The Wayland backend emits the same events as the X11 backend:
- `attach`: Window found and attached
- `focus`: Window gained focus
- `blur`: Window lost focus
- `detach`: Window destroyed
- `fullscreen`: Fullscreen state changed
- `moveresize`: Window position or size changed

## Usage

### Automatic Detection
The library automatically detects the display server and uses the appropriate backend:

```typescript
import { OverlayController } from 'electron-overlay-window';

const overlayWindow = new BrowserWindow({
  // ... window options
});

// Automatically uses Wayland or X11 backend
OverlayController.attachByTitle(overlayWindow, 'Target Window Title');
```

### Environment Variables
To force Wayland detection, set:
```bash
export WAYLAND_DISPLAY=wayland-0
export XDG_SESSION_TYPE=wayland
```

## Building

### Prerequisites
On Linux systems with Wayland support, install:
```bash
# Ubuntu/Debian
sudo apt-get install libwayland-client0 libwayland-dev libxkbcommon-dev

# Fedora
sudo dnf install wayland-devel xkbcommon-devel

# Arch Linux
sudo pacman -S wayland xkbcommon
```

### Build Process
The library automatically includes Wayland support when building on Linux:
```bash
npm install
npm run build
```

## Limitations

### Current Limitations
1. **KDE Plasma Only**: Currently only supports KDE Plasma window management
2. **Experimental**: This is experimental support and may have issues
3. **Protocol Dependencies**: Requires KDE Plasma window management protocols
4. **Limited Testing**: Limited testing on different Wayland compositors

### Known Issues
1. May not work with non-KDE Plasma Wayland compositors
2. Screenshot functionality not implemented for Wayland
3. Some advanced window management features may not work

## Testing

### Manual Testing
1. Ensure you're running on a Wayland session with KDE Plasma
2. Run the demo application:
   ```bash
   npm run demo:electron
   ```
3. Check console output for Wayland detection messages

### Environment Verification
```bash
echo $WAYLAND_DISPLAY
echo $XDG_SESSION_TYPE
```

## Future Improvements

### Planned Enhancements
1. **GNOME Support**: Add support for GNOME's window management protocols
2. **Generic Wayland**: Implement generic Wayland window tracking
3. **Screenshot Support**: Add screenshot functionality for Wayland
4. **Better Error Handling**: Improve error handling for unsupported environments

### Contributing
To contribute to Wayland support:
1. Test on different Wayland compositors
2. Report issues with specific environments
3. Implement support for additional protocols
4. Improve error handling and fallback mechanisms

## Troubleshooting

### Common Issues

#### "No compositor found"
- Ensure you're running on a Wayland session
- Check that KDE Plasma window management is available
- Verify environment variables are set correctly

#### "Failed to connect to Wayland display"
- Ensure Wayland is properly configured
- Check that the display server is running
- Verify Wayland libraries are installed

#### Overlay not appearing
- Check that the target window title matches exactly
- Verify the overlay window is properly configured
- Check console for error messages

### Debug Information
Enable debug logging by checking console output for:
- Wayland detection messages
- Window management protocol availability
- Event emission logs

## References

- [Wayland Protocol](https://wayland.freedesktop.org/)
- [KDE Plasma Wayland Protocols](https://github.com/KDE/plasma-wayland-protocols)
- [Original Issue](https://github.com/SnosMe/electron-overlay-window/issues/28) 