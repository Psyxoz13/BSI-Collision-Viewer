# CollisionViewer

Draws BioShock Infinite collision on top of the running game: PhysX meshes, engine brushes and movers, triggers and character cylinders.

It is a `d3d11.dll` the game picks up from its own folder. `D3D11CreateDevice` goes through it, every other export is passed to the system `d3d11.dll`, and the overlay is drawn into the game's own frames just before they are presented.

## Build

Visual Studio 2026 with the C++ desktop workload (toolset v145). The game is 32-bit, so the only platform is Win32:

```
msbuild CollisionViewer.vcxproj /p:Configuration=Release /p:Platform=Win32
```

The DLL lands in `build\Release\d3d11.dll`. It links the C++ runtime statically and needs Windows 8 or newer.

## Use

Copy `d3d11.dll` into `Binaries\Win32` next to `BioShockInfinite.exe`; delete it to play the unmodified game again.

F8 toggles the overlay, F7 opens the settings, F6 toggles the player state panel; all three can be rebound, with Ctrl, Shift and Alt if you hold them while binding. While the settings are open the game gets no mouse or keyboard and the cursor is free.

The player state panel reads the player's pawn every frame, in the game's own units. Each row is switched on separately under Rows:

- velocity, horizontal and vertical, each beside the highest value seen since the pawn last stood still
- the physics state it is in
- whether it collides with the world
- the radius and height of its collision cylinder
- its position

The panel takes no input, so it is placed from the settings rather than dragged.

Beside the DLL:

- `CollisionViewer.ini` keeps the settings
- `CollisionViewer.imgui.ini` keeps where the settings window was
- `CollisionViewer.log` says what was found in the game's memory, what each loaded level contains and what was skipped

Dear ImGui 1.91.9b is vendored in `third_party\imgui` under its MIT license.
