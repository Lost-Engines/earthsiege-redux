# Earthsiege Redux

[![Build and release](https://github.com/Lost-Engines/earthsiege-redux/actions/workflows/build.yml/badge.svg)](https://github.com/Lost-Engines/earthsiege-redux/actions/workflows/build.yml)

[Download the latest release](https://github.com/Lost-Engines/earthsiege-redux/releases/latest)
for Linux, Windows or macOS Apple Silicon. Extract the archive and run `earthsiege`
(or `earthsiege.exe`); the original game data is included.
See [builds and releases](docs/build-release.md) for platform requirements, CI checks
and the publication process.


Earthsiege Redux builds on the native C++26 reconstruction of **Metaltech: Earthsiege**. It uses the original 1994 version 1.0 resources and renders the 3D world at modern display resolutions. Single missions and careers run from registration and briefing through combat, debrief, repair and progression, with the original cinematics, music, effects and speech.

The March 1995 CD expansion is included alongside the original game. The launcher offers a choice of editions; `--original` and `--expansion` start either directly. Both battlesets, career transfer, Apocalypse, the new enemies and weapons, deployment pods, environments, music and CD cinematics are connected. All four expansion demos now match the original player motion and gameplay RNG throughout; the [replay audit](docs/DEMO_REPLAY.md) covers 11,071 simulation updates. [Expansion integration](docs/EXPANSION.md) describes the original-code comparisons and the limits of that coverage. The 1996 base-game reissue has unchanged 1.0 game files.

The Linux test suite replays all eight shipped demos. For the four retail recordings it also compares the mission results and campaign variables byte for byte. See the [validation report](docs/VALIDATION.md).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/earthsiege
```

Requires a C++26 compiler, CMake 3.30 or newer and OpenGL development files. CMake builds GLFW from upstream `master` and fetches pinned miniaudio, Munt, Nuked-SC55 and TinySoundFont sources on the first configure. Original freeware game resources are bundled in `assets`. Use `--data-dir PATH` to select another installation. GLFW handles display and input, and miniaudio supplies PCM output. See [third-party code](THIRD_PARTY.md) and [platform implementation](docs/PLATFORM.md).

### Render resolution

By default the game draws at the window's current pixel dimensions, including on HiDPI displays. Resizing the window gives HERC silhouettes, terrain textures and the 3D briefing map more or fewer pixels of detail. The cockpit, menus and cinematics use their original pixel art. The picture keeps its 8:5 proportions, with black borders where needed.

```sh
./build/earthsiege --resolution 1280x800
./build/earthsiege --scale 3
./build/earthsiege --resolution 320x200 --scale 4
```

The first command fixes the render size at 1280×800, even if you resize the window. The second opens a 960×600 window and follows subsequent resizes. The third gives the original 320×200 rendering in a 1280×800 window. Both editions accept the same options.

During the first turn in demo 2, the DOS sky and ground can leave a strip of a passing building behind. Redux paints that gap with the world's far-ground colour.

### Model detail and draw distance

`--lod-distance-multiplier N` scales the distance at which HERCs, mounted weapons and other model parts switch to simpler geometry. The Shapes setting determines the finest detail available.

`--distance-cutoff-multiplier N` scales how far you can see buildings, HERCs and terrain models. Both multipliers default to `10`, ten times the original distances. You can set either one to `1` for the DOS distance or use a positive fraction for a shorter range. Fog follows the mission's original distances. Objects visible beyond those distances keep their farthest tint.

In DOS, a distant building could lose its silhouette in the haze while its painted panels still stood out. Redux fades textures and fixed model colours through the mission's distance bands. Up close, a gun tower's barrel openings turn from black to bright colours as it fires. With distance, the haze softens the flash and dark details on HERC weapons until they take on the local fog colour.

```sh
./build/earthsiege --demo 2 --lod-distance-multiplier 1 --distance-cutoff-multiplier 1
```

### Cockpit shields

Both editions use the expansion's shield gauge. An empty front or rear shield goes black. As it recharges, one green band appears first. The other two start to light above 25% and 37.5% charge. The numbers beside the gauge show the requested front and rear allocation.

The recharge cap follows the edition you launch. With enough reactor power, a HERC regains up to four shield points per simulation update in the original and five in the expansion, even on a chassis available in both. The original armory takes recovered weapons in their surviving condition. The expansion returns eligible weapons at 100% and excludes the three Cybrid-only types.

### Map and wingmate orders

Both campaigns open the cockpit on the scanner. Contacts turn with the HERC's torso, using the correction from the floppy expansion and the April patch. The lower tactical map fits its zoom range to the mission area and scales the pan step with zoom. You can assign an attack or defence beyond the scanner's selected range.

Give a wingmate DEFEND on a friendly HERC and they follow it as it moves. Click open ground and they guard that spot, even if the previous order named a HERC. Wingmates steer clear of your firing line. While following you, they remember whether you gave the order to fire at will or hold fire when deciding whether to look for enemies. A wingmate guarding a mission objective considers the distance to that objective when deciding whether to give chase. Coming under fire can prompt retaliation while preserving an explicit order.

HERCs use the expansion's steering when backing away from an obstruction or closing on a flyer. Automatic aiming stops when the torso or guns reach the target angle. The radio uses the April patch's fix for an interrupted transmission so the next call can start.

### Projectile timing and armour penetration

Ordinary bullets in both campaigns use elapsed simulation time for their lifetime. In retail DOS a bullet spent one unit of lifetime on every update, even when that update moved it only a fraction of its usual distance. The expansion clock keeps its range consistent across short and long updates.

When a hit breaks armour, the next component takes the excess. With 10 points of armour left, a 15-point hit passes 5 points inward, and a 75-point hit passes 65. Expansion DOS passed **-10** in both cases. The damage system reads a negative component value as destroyed, so even a hit that barely pierced armour could take off a leg. Redux uses the full sum before subtracting the armour capacity.

### Impact fragments

`--fragment-count-multiplier N` scales the maximum number of loose pieces thrown by each impact burst. At the Redux default of `4`, hits can throw from zero to 8, 16 or 24 pieces, depending on the damage. Fractions round the maximum down. At `0`, hits can still knock a weapon off its mount.

`--fragment-pool-size N` limits how many loose pieces and detached weapons can be in flight at once. The default is `600`. Once the pool fills, further pieces are dropped until a slot opens. A pool of `0` hides both kinds of debris. Damage, smoke and explosions still work.

To play with the original debris density, use the DOS limits.

```sh
./build/earthsiege --demo 2 --fragment-pool-size 150 --fragment-count-multiplier 1
```

### Randomness and recorded demos

Redux seeds a standard C++ Mersenne Twister (`std::mt19937`) from `std::random_device` for interactive play. Mission choices, AI decisions, combat and particle bursts draw from it. Replaying the same mission can give you different opponents, shots and damage rolls wherever the game calls for chance.

The eight shipped demos record the pilot's controls. A different combat roll can destroy a HERC the pilot was about to shoot, so playback uses the original random sequence and the recording's map, orders, projectile clock and damage arithmetic. Both fragment settings work during playback. With `--inspect-demo` you fly the same starting mission under live Redux rules.

Options → View Demo picks one of the four recordings for the selected edition at random. Use `--demo N` (1-4) to choose one yourself.

```sh
./build/earthsiege --demo 1
./build/earthsiege --expansion --demo 3
```

The second command plays the Apocalypse patrol on the red world. `--demo` skips the opening screens and defaults to the original game unless `--expansion` or another `--data-dir` is supplied. Press Escape to return to the main menu. Clicking the window or using Alt+Tab leaves the demo running. Preferences → Detail cycles through Low, Medium, High and Max; Accept saves it and Cancel restores the previous setting.

`--inspect-demo 1` (or 2–4) opens a recorded mission’s starting terrain and HERC roster. Use arrows/keypad to walk and turn, I/J/K/M or right mouse to aim, and Space to fire. W/Alt+W cycles weapons; L links/unlinks the selected matching weapon pair; Tab cycles targets; Enter confirms tactical-panel commands. F1–F6 select the six MFD pages. Ctrl+F1 shows the cockpit, Ctrl+F2 orbits and Ctrl+F3 detaches. F7/F8 select the lower tactical map/damage panel and F9/F10 the left/right panels. Ctrl+Shift+arrows controls the inspection camera; adding Alt changes distance; P pauses and Escape returns to the menu. The cockpit throttle and weapon charge sliders, shield allocation buttons, weapon groups, LINK and TRACK controls respond to the mouse. Click a numbered weapon to select it, right-click to change its group membership, or use 1–9. The HUD shows heading, torso and pitch indicators, target lock brackets and the next waypoint. The cockpit clock and animated lamps use the original presentation scripts.

The lower panel shows the original tactical map, wingmate orders and structural,
internal and weapon damage diagrams. On the map, 1–3 select a wingmate; the marked
command keys select orders, clicking the map chooses a destination, Tab cycles
eligible contacts, X transmits and Backspace cancels. Double-clicking an already
selected command also transmits when it is ready. The arrow buttons pan or change
the inspected damage subject/page; +/− controls map scale. S/I/W select the three
damage pages. Damage labels use the actual pilot's name.

The MFD displays live status, communications, navigation, radar, target condition and missile-camera views. FLASH COMM sends real wingmate orders; Alt+A/G/H/O/C/E/F provide the original attack, ignore, help, form-up, scan, EMCON and fire/hold shortcuts. Radio acknowledgements now display their original text, pilot portraits and static transitions. Their selection, scheduling, pixels and mouth timing match original executable captures; speech output is connected. MFD damage now uses the original flicker timing and OFFLINE label.

This inspection mode uses mission AI, combat and objectives. It does not replay the recorded inputs. Normal missions are selected from the main menu; Q opens the original mission-status/return dialog, P pauses and Ctrl+Escape or Ctrl+Q asks to quit. F11 displays mission objectives. V opens the external view and N cycles through squad HERCs; Tab/Enter switches camera/HERC control for the player. See the [keyboard audit](docs/KEYBOARD.md) for corrected aliases and the restored original development shortcuts and edition differences.

The original group dispatcher can read an uninitialized priority word for commands such as form up. This reconstruction uses neutral priority for those commands, making delivery deterministic. Radio queues start with zeroed unused indices. Original commander-message loading writes outside its descriptor table; this reconstruction retains the messages as resources and preserves their observed absence from radio output, without copying the memory corruption. The death-report portrait uses a zero offset, matching the captured original allocation; its offset is not initialized by the original asset loader.

Original game assets remain the property of their respective owners. Reverse-engineering notes and comparison tools live in the sibling `earthsiege-reverse` project.

PCM playback retains the original 8-bit mixer’s gain steps, saturation and
buffered completion. The original positional-volume query divides by zero if
queried while the master gain is zero; this reconstruction returns zero in that
case. Unused, manually supplied sample-loop ranges are bounded to their data.

Mission inspection now plays the original PCM effects and wingmate speech through miniaudio. Firing, explosions, radar switches, flyer engines and authored player footsteps use their original triggers and distance/pan rules. Aiming servos, target-lock tones and the cockpit engine loop are connected too. Music retains the original encounter-driven selection and fades. Press **Ctrl+Shift+M** to choose OPL2, OPL3, SC-55, SCC-1A, MT-32/CM-32L or a General MIDI SoundFont; the choice is saved. OPL3 works immediately. `scripts/fetch-assets.sh` (or `.bat`) installs the optional Roland ROMs in the default location. See [music setup and the MT-32 routing finding](docs/MUSIC.md): this release has no melodic tracks assigned to its MT-32 driver, so the full MPU-401-to-MT-32 route is explicitly an experiment.

FM synthesis uses the unmodified LGPL Nuked OPL3 core in `third_party/nuked-opl3`.
The host stream runs at 44.1 kHz; original 11.025 kHz PCM values are held between
DAC updates. Synthesised music and PCM receive equal mix headroom. This reproduces the software
protocol, not a particular Sound Blaster card’s analogue circuitry or balance.

Damage now produces attached smoke and physically ejected weapons, with the original animation timing, attachment transforms and debris motion.

The original lower-panel weapon labels retain shared text references for empty
mounts. After inspecting another HERC, those empty rows can display stale names.
This reconstruction preserves that visible quirk with bounded strings. The map
also preserves the original distinction between mouse selection range and Tab's
remembered-contact selection, and retains unmodified fields in outgoing orders.

Campaign saves preserve the original 41-state career format, ordered equipment inventories and variable hangar records. They pass complete original-executable save/read comparisons, all 396 campaign filename/branch transitions and 140 grade thresholds. Registration, selection, resume and manual backups use these original formats. Saves live in the platform user-data directory (normally `~/.local/share/lost-engines/earthsiege` on Linux); `--save-dir PATH` overrides that location. The bundled resources and comparison DOS installation are not modified.

The portable shell now also implements mission-return salvage, ordered equipment awards/restocking, pilot statistics and replacements, technology unlocks, repair budgets and chassis construction. Complete original return/save comparisons cover 96 scenarios (including 144 replacement pilots); repair checks cover 6,400 quotes, 1,280 transactions and 25 construction states. The arming screen supports original clickable mounts, compatible weapon lists, condition-preserving equipment exchange, missile types and HERC selection. Crew and repair screens are connected; construction controls are now connected too. The service bay now animates its original workers, elevators and welding, including partial-construction artwork. Menu music, workshop sounds and the original weapon turn previews are connected. Briefing maps now include the original guided tour, zoom/pan and object callouts. Commander briefings, chapter introductions, departure remarks and single-mission debriefs now include their original voices, captions and animated portraits. Opening, chapter-travel and ending cinematics are connected, along with campaign mechanic response chains, menu doors and credits. --skip-intro skips the opening and pilot-death movie. Presentation and audio timing limits are described in [the validation report](docs/VALIDATION.md).

With the DOS generator, launch preparation matches all 670 retained original GO launches byte for byte (SCRIPT.DAT, MISSION.VAR and PLAYER.MEC), including the generator state. Four complete new-career constructors and 32 additional roster exports cover history initialization, inactive crew and differing pilot health. The shell's original default missile-type normalization and selected-pilot health export behaviour are retained.

Mission result export now retains recovered weapons and crew damage for the shell. All 5,114 captured recovery/condition states and four complete starting-world handoffs match. The original writer reads beyond its nine equipment slots for a tenth unused condition; this implementation safely writes 100 for that unused word.

The nineteen mission alerts match captured DOS pixels. Six complete campaign
progression journeys now match all 121 launch/reload/return/repair/next-mission
transactions across all three battle sets, including victory and defeat endings
and the original random-mission lists. Each flight return restores the saved inventory.
The fixtures also reproduce the shell seed from that DOS run. Manual career backups remain explicit actions. These comparisons verify
the handoff and persistence, not an entire interactively completed campaign.

Arming comparisons cover 1,650 equipment exchanges, 360 mount menus, 270 missile
selections and 128 HERC-selection cases. The original bay artwork and mount
positions are used. Samson grenade-launcher entries refer to missing bay images
in the supplied data; those layers are omitted safely while keeping the equipment.

Crew Assignment now uses the original portraits, wireframe HERC diagrams and
screen palette. Reassignment preserves the original temporary swap behaviour;
Cancel restores all four assignments. Pilot registration starts with an empty
name field. Repair allocation and policy controls are connected. Construction uses the original blueprint artwork and unlocked chassis selection.

Detailed repairs retain the original condition bands, refunds, cancellation and
Proceed behaviour, including its extra uncharged condition step. Scrap returns
usable equipment to inventory and credits damaged equipment and chassis salvage.
The original repair code corrupts crew-header fields when repairing an unassigned
HERC; this implementation performs the repair without that invalid pilot-share
write. The native comparison covers 5,323 component edits, eight constructions
and 128 scrap/cancel transactions.

Construction menu comparisons cover another 1,424 original events. Unfinished
builds persist across restarts and finish after the original number of mission
returns. The original post-cancellation affordability omission is retained: after
scrapping a partial build, immediately rebuilding can overdraw the material
balance. Empty reserved bays are valid and do not advance construction.

The flight environment now includes the original sky bands, horizon panorama,
projected ground texture, moving clouds, procedural ground marks and
pitch-dependent reflective palette. Combined output matches 288 original
frames across all 24 worlds, including cockpit masks and detail settings.
Live frame scheduling, impact flashes and cockpit displacement are integrated.

The original cinematic fade-in reads an uninitialized rectangle type. This build initializes it, giving deterministic fades instead of depending on prior stack contents.

Cockpit damage flashes the original damage palette and displaces the cockpit and instruments vertically while the outside projection stays steady. The timing and view changes follow the original.

Options / Controls now supports the original joystick profiles, pedals, mouse setting and calibration screens. Tab and Alt+Tab move focus; right-click cycles an option backwards. The first two connected GLFW joysticks supply the original joystick axes, with a single four-axis stick supported as an alternative. Calibrations are saved automatically beside the user saves. Insert or joystick button 2 swaps movement/turret control, button 3 selects a target and button 4 stops movement or centres the turret. Hats follow the original cockpit-view rules.

The bundled DATA/KEYJOY.CFG retains the original pitch/turn choices. Malformed tokens are reported safely instead of reproducing the original out-of-bounds table read. Host joystick calibration uses separate host-joy files so it cannot be confused with DOS gameport counts. See docs/IMPLEMENTATION.md for the host axis and throttle mappings.

Side and lower cockpit views now scroll between panels using the original pixel steps. Music and sound continue during the scroll.

Live missions run a full world step every 100 ms, the DOS simulator's nominal
period. A long cockpit pan uses the original limit on how far one step can
move the world. Demos replay the variable intervals on their tapes. Sound
continues through pans, dialogs and window switches. Switching away releases
any held controls. [How the recording clock works](docs/DEMO_REPLAY.md#how-fast-should-a-demo-play)


F12 or Alt+P opens the original in-flight preferences panel, with a rotating HERC preview. Music, effects, radio mode, horizon, sky, ground and model detail update live. Controls and joystick calibration are available inside it; Escape accepts a panel and returns to its parent. Closing preferences restores the previous flight view.


Cockpit edge strips switch between forward, lower and side views, with the
original directional pointers. Pilot selection uses the authored indicator
lamps. The original unused-portrait clearing quirk is reproduced without its
invalid stack pointer.


External views retain the original camera distance and rotation between switches,
with the original framing and status labels. Hidden clouds pause while the lower
cockpit panel is open. Complete demo checks now include camera state.
If a distorted texture polygon requests pixels outside its image, those pixels
are safely omitted rather than reading unrelated memory.

HERCs now draw their original ground shadows, following the animated feet/body
and disappearing with destroyed parts. Scene lighting and smoke visibility also
retain the original engine's quirks.

Cockpit gauges preserve the original paint timing and incremental speed-bar updates.

The authentic build intentionally fixes a confirmed original bug in distant
radar/listening-post models: invalid references make their drawing order depend
on unrelated memory. The repair gives them stable drawing order, with small
visual differences from DOS. This is a narrow exception to reserving bug fixes
for Redux; reproducing the defect would require extra machinery to imitate the
original memory layout. See [the evidence and pixel comparisons](docs/VALIDATION.md#measured-full-frame-differences).


Authored mission starts now preserve the original order of controller entry,
starting damage and group assignment. Fixed buildings and turrets keep updating
while their group awaits activation; objective references use the original
source-to-pool mapping. These details affect mission behavior beyond the demos.

Two further original memory defects have bounded handling. Negative formation
IDs use formation zero in live sessions, rather than reading before the formation
table. Effect deletion queues can hold every object in their existing fixed
pools, rather than overwriting their own counter after 20 retirements. The pool
limits and cleanup order remain unchanged. These choices avoid reconstructing
undefined DOS heap behavior; the formation fallback is not a claim about the
author's intended layout. The independent comparisons record every affected
reference case. See [mission fidelity and comparison boundaries](docs/VALIDATION.md#authored-missions-and-campaign-journeys).


Two further original bugs are handled explicitly. AI HERCs start with centre
aiming until strategy chooses a component; the original leaves that selection
uninitialized. Long obstacle detours use wide fixed-point multiplication, avoiding
an original overflow bug that can add 262,143 world units to a coordinate. Both
behaviors already followed from the native implementation; they are retained
and now documented with instruction traces and corrected-reference comparisons
in [the validation report](docs/VALIDATION.md#authored-missions-and-campaign-journeys).


Extended comparisons also verify player controls across every supported mission
at two frame ratios and in ten later-career saves. They corrected cockpit/chassis
alignment and the extra animation tick when the player loses mobility, plus the
live blast-centre reference during ramming self-destruction. See [the extended
comparison evidence](docs/VALIDATION.md#authored-missions-and-campaign-journeys).
