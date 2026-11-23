# Platform implementation

Earthsiege uses GLFW for its window, keyboard, mouse and joystick input,
and miniaudio for PCM output. The versions and build approach match Darker and
TFX. SDL is no longer a dependency.

Redux uses the GLFW framebuffer dimensions as its render resolution. A fixed
`--resolution WIDTHxHEIGHT` sets the render target independently of the window.
`--scale N` sets the initial window size in multiples of 320×200. Screenshots
include the whole render canvas and its borders. With no window or fixed
resolution, the target is 320×200.

The software renderer projects world geometry into the pixels available inside
the original 8:5 picture. The ground sampler uses the selected resolution and
the mission's original texture and distance palette. Cockpit instruments, HUD
symbols and menus use the authored 320×200 layout. A cockpit page keeps the
detailed scene behind its window mask so that HUD writes, cursors and page
pans all cover the correct pixels. OpenGL receives the original palette indices
and the 256-colour palette. For each window pixel, the display shader checks
the cockpit mask, samples the artwork or the detailed scene and looks up its
colour. A fully covered scene needs no world texture upload. The shader and
the screenshot renderer use the same nearest-neighbour cell boundaries. Mouse
input and cursor warps use the same fitted rectangle, including on HiDPI displays.

The launch lift has shorter frame intervals than ordinary driving. The first
115 frames of retail demo 2 take 3.296 seconds on its recorded clock. To replay
the lift and its final shake at 4K, run
`./build/earthsiege --original --demo 2 --resolution 3840x2160 --frames 150`.
At high resolutions, the ground rasterizer shares large draws across CPU cores.
With a level horizon it calculates depth and fog once per scanline. The horizon
bitmap scales by filling the screen area covered by each run of like-coloured
texels, leaving transparent pixels alone.

GLFW callbacks append to an ordered input queue. A mission modal can stop
consuming that queue and resume it at the original simulation call site, so a
quick Pause/Escape pair retains its order. Character input is separate from
physical key commands. Focus loss releases held controls and cancels pressed
buttons. The simulation, music and effects continue while another window has focus.
Ctrl+Shift+M opens the in-window music selector; keyboard and mouse selection,
Cancel and missing-device errors use the same GLFW window.

The first two connected joysticks retain their slots until disconnected. Raw
GLFW axes are converted to the existing 0-65535 host range, preserving the
original device profiles, saved host calibrations, throttle, pedal fallback and
hat mappings. GLFW's synthetic hat buttons are disabled so they cannot become
extra weapon buttons. A test source exercises device connection, removal and
sample conversion without requiring physical controllers on CI.

The game's existing mixer and synthesiser backends produce signed 16-bit,
44.1 kHz stereo samples. A synchronized queue feeds those samples to miniaudio.
Playback starts with four device periods buffered (about 23 ms at 44.1 kHz).
After an underrun the queue builds up that reserve again. Clearing the queue
discards audio from the previous scene. Miniaudio performs any conversion
needed by the host output device.

Existing saves and preferences remain in the same locations:

- Linux: `$XDG_DATA_HOME/lost-engines/earthsiege`, or
  `~/.local/share/lost-engines/earthsiege`.
- Windows: `%APPDATA%/lost-engines/earthsiege`.
- macOS: `~/Library/Application Support/lost-engines/earthsiege`.

`--save-dir` still overrides that location. Native executable-path discovery
finds bundled assets and optional ROMs even when launched outside the package
directory. ROM discovery keeps its existing rules documented in [MUSIC.md](MUSIC.md).

GCC compiles C and C++ in the macOS release build. As in the sibling projects,
Apple Clang compiles the Cocoa and CoreAudio sources as Objective-C. The game
continues to target C++26.

## Checks

`platform_transport` covers square-pixel viewports, HiDPI cursor round trips,
letterbox input and queued PCM pause/clear/underrun behaviour. `joystick_devices`
covers host-device selection and the original axis/button/hat mappings.

Configure with `-DEARTHSIEGE_TEST_WINDOW=ON` to add `platform_window`, which needs
a desktop display. It exercises the actual GLFW callbacks, event ordering,
Unicode text, focus, mouse coordinates and close events, then reads back the
OpenGL presentation to check orientation and colour channels. It is an opt-in
local test; CI disables real-window checks. Package execution checks are also
local-only, selected explicitly with `--run` or `--window`.

The migration passed all 272 standard tests and the additional window check.
A separate X11 keyboard check exercised music selection, saved preferences,
Cancel and recovery from a missing SoundFont. The original-executable comparison
suite covers gameplay, the 320×200 renderer and music scheduling. The Redux
window check also reads back a full composited frame and compares it with the
software renderer after a resize, a palette change and a full-screen cover. It
checks a cropped cockpit page with a fixed render size in a differently sized
window as well.
