# Changelog

## 0.2.0 (first public release)

- Hosts Audio Unit effects as pedals in a serial chain, inside Logic Pro or any AU host, or as a standalone app.
- Drag pedals to reorder the chain; the others slide aside to show where it will land. Move Left / Move Right in the pedal's menu as well.
- Footswitch = true bypass: the pedal is wired around and switched into its cheap bypass mode.
- MIDI footswitch control: MIDI Learn per pedal (CC or note), "toggle on each press" or "follow switch" behaviour, reads MIDI devices directly so no routing is needed.
- The selected pedal's own UI is shown below the board; UIs too big for the screen open in their own window (Pop Out / Dock).
- The whole board (order, bypass, MIDI assignments, every pedal's settings) is saved with the host project.
- Pedals whose plug-in isn't installed keep their place and settings instead of being dropped.
- Reports the chain's latency to the host, including when a pedal changes its own latency.
- Universal binary (Apple Silicon + Intel), macOS 11 or later.

Earlier development builds were called "Pedalboard". Tonechain uses the same plug-in codes, so projects saved with those builds open with Tonechain.
