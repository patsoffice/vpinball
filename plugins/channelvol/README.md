# Channel Volume Plugin

This plugin allows per-table OS-level audio volume control for backglass and SSF (Surround Sound Feedback) speakers. Volume settings are saved per table and automatically applied when a table is loaded, then restored to the original levels on exit.

## Setup

Enable the plugin and configure hotkeys in VPinballX.ini:

```ini
[Plugin.ChannelVol]
Enable = 1
BGVolumeUpKey = 82
BGVolumeDownKey = 81
SSFFrontVolumeUpKey = 80
SSFFrontVolumeDownKey = 79
SSFRearVolumeUpKey = 78
SSFRearVolumeDownKey = 77
VolumeStep = 5
```

The key values are SDL scancodes. Some common ones:

- 79-82: Arrow keys (Right, Left, Down, Up)
- 75-78: PageUp, PageDown, End, Home
- 58-69: F1-F12

Choose keys that don't conflict with VPX's existing bindings for your setup.

## Using

During gameplay, press the configured hotkeys to adjust volume levels. A notification will appear showing the current level (e.g., "Backglass: 75%"). The plugin adjusts the OS system mixer volume for the audio devices configured in VPX.

Volume changes are automatically saved to the table's INI file and restored on the next play. On game exit, the original OS volume levels are restored.

## Settings

### Global settings (VPinballX.ini)

| Setting | Description | Default |
|---------|-------------|---------|
| BGVolumeUpKey | SDL scancode for backglass volume up | 0 (disabled) |
| BGVolumeDownKey | SDL scancode for backglass volume down | 0 (disabled) |
| SSFFrontVolumeUpKey | SDL scancode for SSF front volume up | 0 (disabled) |
| SSFFrontVolumeDownKey | SDL scancode for SSF front volume down | 0 (disabled) |
| SSFRearVolumeUpKey | SDL scancode for SSF rear volume up | 0 (disabled) |
| SSFRearVolumeDownKey | SDL scancode for SSF rear volume down | 0 (disabled) |
| VolumeStep | Volume change per keypress (%) | 5 |

### Per-table settings (saved automatically to table INI)

| Setting | Description | Default |
| ------- | ----------- | ------- |
| BackglassVolume | Backglass device volume (0-100) | -1 (use current) |
| SSFFrontVolume | SSF front speaker volume (0-100) | -1 (use current) |
| SSFRearVolume | SSF rear speaker volume (0-100) | -1 (use current) |

A value of -1 means the plugin will not change the OS volume for that channel group.

## Channel mapping

- **Backglass**: All channels on the backglass audio device
- **SSF Front**: Channels 0 (FL) and 1 (FR) on the playfield audio device
- **SSF Rear**: Channels 4 (BL) and 5 (BR) on the playfield audio device

## Visual feedback

Volume changes are shown using VPX's built-in notification system (text toasts) rather than graphical volume bars. The VPX plugin ancillary renderer API only supports rendering to secondary windows (Backglass, ScoreView, Topper) — not the main playfield window. Since the playfield is the primary display during gameplay and many setups may not have a separate backglass window, text notifications provide the most reliable feedback across all configurations.

## Platform support

- **macOS**: CoreAudio backend. Works with devices that expose volume control (built-in speakers, most audio interfaces). USB headsets and HDMI outputs with hardware-only volume are not controllable.
- **Linux**: Planned (PulseAudio/PipeWire and ALSA backends).
