# unreal-ai-tools

An Unreal Engine 5.7 editor plugin plus a Python CLI that drives it — built so an
external agent (or a human) can inspect and manipulate a running Unreal editor
without touching the editor UI.

**UnrealAITools** (C++ plugin) starts an in-process HTTP server inside the editor
exposing:
- **JSON data assets** — create/sync/read/list/validate property-bag data assets from JSON, plus reference lookups
- **Generic asset ops** — move/delete/list any asset, reference-safe
- **Observer cameras** — place a named viewpoint and ask "what does it see" (frustum + occlusion, returns compact JSON, no pixel rendering)
- **Scene objects** — create/remove/list/transform (move/rotate/scale) actors by name

**ai-tools-cli** (Python, `uaicli` command) talks to that HTTP server.

## Repo layout

```
UnrealAITools/   the plugin (Source/, AITools.uplugin)
ai-tools-cli/    the CLI (cli/, core/, mcp_server/, pyproject.toml)
```

## Setup

1. Drop/symlink `UnrealAITools/` into your `.uproject`'s `Plugins/` folder and
   enable "AI Tools" in the editor's plugin list (or add it to
   `.uproject`'s `"Plugins"` list directly). Build the project — the plugin
   compiles as an Editor module.
2. Install the CLI:
   ```
   cd ai-tools-cli
   uv sync
   ```

## Usage

The CLI auto-detects your `.uproject`: if you're inside/under the project
directory it walks up to find it; if one or more editors are already running
it reads the project from the running process list (prompts you to pick one
if there's more than one). Otherwise pass `--project path\to\Game.uproject`.

```
uaicli editor start [--headless]        # launch the editor with the HTTP server
uaicli editor status                    # is it running, on what port/pid
uaicli shell                            # interactive mode -- connect once, run many commands

uaicli data-asset sync my_asset.json    # create/update a JSON-defined data asset
uaicli data-asset list --type MyType
uaicli asset move /Game/Old /Game/New   # reference-safe rename/move

uaicli object create Cube1 0 0 100      # spawn a cube at (0,0,100)
uaicli transform set Cube1 --x 200 --yaw 45

uaicli observer place Cam1 0 -500 200 --pitch -10
uaicli observer describe Cam1           # what Cam1 currently sees, as JSON
```

Run `uaicli --help` or `uaicli <group> --help` for the full command surface.
