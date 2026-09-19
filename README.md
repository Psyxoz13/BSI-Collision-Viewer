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

F8 toggles the overlay, F7 opens the settings; both keys can be changed under Hotkeys in the settings. While the settings are open the game gets no mouse or keyboard and the cursor is free.

Beside the DLL:

- `CollisionViewer.ini` keeps the settings
- `CollisionViewer.imgui.ini` keeps where the settings window was
- `CollisionViewer.log` says what was found in the game's memory, what each loaded level contains and what was skipped

Dear ImGui 1.91.9b is vendored in `third_party\imgui` under its MIT license.
