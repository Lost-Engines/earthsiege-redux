# Earthsiege Redux

In Dynamix's 1994 mech simulator *Metaltech: Earthsiege*, you command a squad of armoured walkers called HERCs against the Cybrids, the machines that have overrun Earth. Redux adds higher resolutions, longer views and combat fixes to the [faithful reconstruction of Earthsiege](https://github.com/Lost-Engines/earthsiege-cpp).

## Playing the game

Earthsiege and its expansion are bundled in the [Windows, Linux and Apple Silicon Mac downloads](https://github.com/Lost-Engines/earthsiege-redux/releases/latest).

## Building from source

Redux uses the [same build tools and platform prerequisites as Earthsiege C++](https://github.com/Lost-Engines/earthsiege-cpp#building-from-source).

```sh
git clone https://github.com/Lost-Engines/earthsiege-redux.git
cd earthsiege-redux
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build --parallel
./build/earthsiege
```

The checkout includes the game data and Sound Blaster music. For Roland music, fetch the optional ROMs with `./scripts/fetch-assets.sh` on Linux or macOS, or `.\scripts\fetch-assets.bat` from PowerShell on Windows. Press **Ctrl+Shift+M** in the game to choose a device. [Music setup](https://github.com/Lost-Engines/earthsiege-cpp#optional-roland-music)

## Bug fixes

- Banking the cockpit no longer leaves black strips or pieces of passing buildings on the horizon.
- Scanner contacts stay in the right direction when you turn your HERC's torso.
- Rotating radar dishes draw correctly at long range.
- When a hit pierces armour, only the excess damage reaches the part underneath. In the DOS expansion, a shot that barely penetrated could cost you a healthy leg.
- Bullet range no longer depends on how often the game updates.
- Wingmates guard the spot you designate even if their last order was to escort a HERC.
- The radio can play the next call after a transmission is interrupted.

## Game improvements

- The 3D world and briefing map render at your window's resolution. The cockpit and menus keep their original pixel art.
- Models stay detailed and visible ten times farther away by default. You can adjust detail and draw distance independently.
- Fog fades textures, dark gun barrels and muzzle flashes along with the rest of a distant model.
- Hits can throw more debris. You can change how many pieces fly off and how many can be in the air at once.
- Both campaigns start on the cockpit scanner and use the expansion's shield gauge, which goes dark when empty and lights up in stages as it recharges.
- The tactical map fits the mission area. You can give attack and defend orders beyond scanner range.
- Wingmates steer clear of your line of fire and remember hold-fire orders while following you. HERCs back away from obstacles more reliably and stop turning their guns when they're on target.
- A new random generator varies encounters and combat in live missions. The eight recorded demos use the original rolls.

## Command-line options

| Option | What it does |
| --- | --- |
| `--resolution WxH` | Set a fixed render size, such as `1920x1080`. Use `window` (the default) to follow the window size. |
| `--scale N` | Set the initial window size to a multiple of 320×200. |
| `--lod-distance-multiplier N` | Set how far away models switch to simpler shapes. Default 10. Use 1 for the DOS distances. |
| `--distance-cutoff-multiplier N` | Set how far you can see HERCs, buildings and terrain models. Default 10. Use 1 for the DOS distances. |
| `--fragment-pool-size N` | Limit the number of airborne fragments and detached weapons. Default 600. |
| `--fragment-count-multiplier N` | Multiply the maximum debris per impact. Default 4. Use 0 to turn off loose fragments. |
| `--original` or `--expansion` | Start the chosen game directly. |
| `--skip-intro` | Skip the opening and pilot-death cinematics. |
| `--demo N` | Watch demo 1 to 4. Add `--expansion` for the expansion recordings. |
| `--inspect-demo N` | Take the controls in the starting mission from demo 1 to 4. |
| `--audio-setup` | Choose a music device at startup. |
| `--music NAME` | Choose `opl2`, `opl3` (default), `sc55`, `scc1a`, `mt32`, `mt32-mpu` or `soundfont`. |
| `--soundfont FILE` | Use an SF2 file with the `soundfont` music option. |
| `--rom-dir PATH` | Look for Roland ROMs in this folder. |
| `--data-dir PATH` | Use game data from another installation. |
| `--save-dir PATH` | Choose where to store saved games. |
| `--help` | Show the available options. |

<details>
<summary>Debug builds only</summary>

`--headless` runs without a window, `--frames N` sets a frame limit, `--screenshot FILE` saves a PPM image, and `--list-resources` prints file sizes and checksums.

</details>
