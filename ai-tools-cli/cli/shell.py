"""Interactive mode: `umcp shell` -- connect once (one EditorClient, one HTTP
connection), then run any number of commands without repeating --project/--port
or paying a fresh Python-process/argument-parsing cost per command.

Reads one command per line from stdin, so it works both as a human-typed REPL
and as a batch driven by a pipe or heredoc:

    umcp --project C:\\...\\AITT.uproject shell <<'EOF'
    types
    sync C:\\...\\Content\\Items\\HealthPotion.json
    listjsonassets
    EOF
"""

from __future__ import annotations

import cmd
import json
import shlex
import sys
from pathlib import Path

try:
    import readline  # noqa: F401  -- On Windows this is pyreadline3; importing it is what
    # gives cmd.Cmd's use of input() arrow-key history/editing at all (stdlib has no
    # readline on Windows). Unused directly here except for read/write_history_file below.
except ImportError:  # pragma: no cover - readline unavailable even on POSIX (rare)
    readline = None

from core import (
    asset_ops,
    file_ops,
    json_data_asset as ops,
    observer_ops,
    scene_object_ops,
    transform_ops,
)
from core.editor_client import EditorClient, EditorClientError

from .common import asset_path_to_json_file, format_json, resolve_asset_path

HISTORY_FILE = Path.home() / ".umcp_history"
HISTORY_LENGTH = 1000


def _split_args(arg: str) -> list[str]:
    """Splits a command's argument string, tolerating unescaped Windows paths
    (posix=True shlex treats backslash as an escape char and mangles them)."""
    tokens = shlex.split(arg, posix=False)
    return [t[1:-1] if len(t) >= 2 and t[0] == t[-1] and t[0] in "\"'" else t for t in tokens]


class InteractiveShell(cmd.Cmd):
    def __init__(self, client: EditorClient) -> None:
        super().__init__()
        self.client = client
        interactive = sys.stdin.isatty()
        self.use_rawinput = interactive

        # Tag the prompt with the controlled editor's PID, and flag it loudly if that
        # editor is headless (-nullrhi, nothing visible to look at) -- to catch exactly the
        # "it says it moved but nothing changed on screen" confusion of talking to the wrong
        # (or a headless) editor instance.
        pid, headless = client.pid_and_headless()
        if pid is None:
            tag = "umcp"
        elif headless:
            tag = f"umcp@headless:{pid}"
        else:
            tag = f"umcp@{pid}"
        self.prompt = f"{tag}> " if interactive else ""

        self.intro = (
            f"Connected to {client.base_url} (project: {client.project}). "
            "Type 'help' for commands, 'exit' to quit."
            if interactive
            else None
        )

    def preloop(self) -> None:
        if readline and self.use_rawinput:
            readline.set_history_length(HISTORY_LENGTH)
            try:
                readline.read_history_file(HISTORY_FILE)
            except OSError:
                pass  # no history yet, or unreadable -- start fresh

    def postloop(self) -> None:
        if readline and self.use_rawinput:
            try:
                readline.write_history_file(HISTORY_FILE)
            except OSError:
                pass

    def onecmd(self, line: str) -> bool:
        try:
            return bool(super().onecmd(line))
        except EditorClientError as exc:
            print(f"error: {exc}")
            if exc.data and exc.data.get("referencedBy"):
                print("referenced by:")
                for referencer in exc.data["referencedBy"]:
                    print(f"  {referencer}")
            return False
        except (OSError, ValueError, json.JSONDecodeError) as exc:
            print(f"error: {exc}")
            return False

    def emptyline(self) -> bool:
        return False  # don't repeat the last command on a blank line

    def default(self, line: str) -> bool:
        print(f"unknown command: {line!r} (type 'help')")
        return False

    # ---- commands -------------------------------------------------------

    def do_create(self, arg: str) -> None:
        """create <asset-path> <json> -- create/upsert from inline JSON, e.g.:
        create /Game/Items/HealthPotion {"assetType": "X", "fields": {}}
        Also writes the matching Content/ .json file -- edit that file and
        `sync` it for any change after the first `create`."""
        arg = arg.strip()
        brace_index = arg.find("{")
        if brace_index == -1:
            print("usage: create <asset-path> <json>")
            return
        asset_path = arg[:brace_index].strip()
        if not asset_path:
            print("usage: create <asset-path> <json>")
            return
        payload = json.loads(arg[brace_index:])

        json_path = asset_path_to_json_file(self.client.project, asset_path)
        json_path.parent.mkdir(parents=True, exist_ok=True)
        json_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
        print(f"wrote {json_path}")

        print(format_json(ops.sync_json_data_asset(self.client, asset_path, payload)))

    def do_sync(self, arg: str) -> None:
        """sync <json-path> -- reconcile an existing data asset from an edited JSON file
        (see `create` to make the file in the first place)"""
        args = _split_args(arg)
        if len(args) != 1:
            print("usage: sync <json-path>")
            return
        json_path = Path(args[0])
        payload = json.loads(json_path.read_text(encoding="utf-8"))
        asset_path = resolve_asset_path(self.client.project, json_path)
        print(format_json(ops.sync_json_data_asset(self.client, asset_path, payload)))

    def do_read(self, arg: str) -> None:
        """read <asset-path> -- read a data asset back out as JSON"""
        args = _split_args(arg)
        if len(args) != 1:
            print("usage: read <asset-path>")
            return
        print(format_json(ops.read_data_asset_as_json(self.client, args[0])))

    def do_listjsonassets(self, arg: str) -> None:
        """listjsonassets [folder] [type] -- enumerate JSON-backed data assets specifically
        (see `listassets` for any Unreal asset, `objects` for actors placed in the scene)"""
        args = _split_args(arg)
        folder = args[0] if len(args) > 0 else None
        asset_type = args[1] if len(args) > 1 else None
        print(format_json(ops.list_data_assets(self.client, folder, asset_type)))

    def do_listassets(self, arg: str) -> None:
        """listassets [folder] [class] -- enumerate any registered Unreal asset (StaticMesh,
        Texture, Material, JSON data assets, anything), optionally filtered by folder and/or
        class name (e.g. "StaticMesh"). See `listjsonassets`/`objects` for narrower views."""
        args = _split_args(arg)
        folder = args[0] if len(args) > 0 else None
        class_name = args[1] if len(args) > 1 else None
        print(format_json(asset_ops.list_assets(self.client, folder, class_name)))

    def do_referencers(self, arg: str) -> None:
        """referencers <asset-path> -- JSON data assets referencing <asset-path> (indexed
        lookup, doesn't iterate every data asset). Use before moving/renaming/deleting it."""
        args = _split_args(arg)
        if len(args) != 1:
            print("usage: referencers <asset-path>")
            return
        print(format_json(ops.find_referencing_data_assets(self.client, args[0])))

    def do_move(self, arg: str) -> None:
        """move <src> <dst> -- move/rename anything: a registered asset (fixes up every
        reference project-wide), a JSON source file, or any other file. Accepts /Game/...
        paths or filesystem paths; a .uasset's .json source (if any) moves with it."""
        args = _split_args(arg)
        if len(args) != 2:
            print("usage: move <src> <dst>")
            return
        print(format_json(file_ops.move(self.client, args[0], args[1])))

    do_rename = do_move

    def do_delete(self, arg: str) -> None:
        """delete <path> [force] -- delete anything: a registered asset (refused if still
        referenced, unless "force"), a JSON source file, or any other file"""
        args = _split_args(arg)
        if len(args) not in (1, 2):
            print("usage: delete <path> [force]")
            return
        force = len(args) == 2 and args[1].lower() in ("force", "true", "1")
        print(format_json(file_ops.delete(self.client, args[0], force)))

    def do_observe(self, arg: str) -> None:
        """observe <name> <x> <y> <z> [pitch] [yaw] [roll] [fov] -- place/move a named
        observer camera (upsert). Works in the editor and during Play-In-Editor."""
        args = _split_args(arg)
        if len(args) < 4:
            print("usage: observe <name> <x> <y> <z> [pitch] [yaw] [roll] [fov]")
            return
        name = args[0]
        numbers = [float(a) for a in args[1:]]
        x, y, z = numbers[0], numbers[1], numbers[2]
        pitch = numbers[3] if len(numbers) > 3 else 0.0
        yaw = numbers[4] if len(numbers) > 4 else 0.0
        roll = numbers[5] if len(numbers) > 5 else 0.0
        fov = numbers[6] if len(numbers) > 6 else 90.0
        print(format_json(observer_ops.place_observer(self.client, name, x, y, z, pitch, yaw, roll, fov)))

    def do_observers(self, arg: str) -> None:
        """observers -- list observer cameras placed in the current world"""
        print(format_json(observer_ops.list_observers(self.client)))

    def do_unobserve(self, arg: str) -> None:
        """unobserve <name> -- remove a named observer camera"""
        args = _split_args(arg)
        if len(args) != 1:
            print("usage: unobserve <name>")
            return
        print(format_json(observer_ops.delete_observer(self.client, args[0])))

    def do_spawn(self, arg: str) -> None:
        """spawn <name> <x> <y> <z> [pitch yaw roll] [sx sy sz] [mesh <path>] -- create/move
        a static mesh object (upsert). Mesh defaults to the engine's basic cube; append
        "mesh <path>" at the end to use a different one, e.g.:
        spawn Prop1 300 0 50 mesh /Game/Items/Foo.Foo"""
        args = _split_args(arg)
        mesh = ""
        if "mesh" in args:
            idx = args.index("mesh")
            if idx + 1 < len(args):
                mesh = args[idx + 1]
            args = args[:idx]
        if len(args) < 4:
            print("usage: spawn <name> <x> <y> <z> [pitch yaw roll] [sx sy sz] [mesh <path>]")
            return
        name = args[0]
        numbers = [float(a) for a in args[1:]]
        x, y, z = numbers[0], numbers[1], numbers[2]
        pitch = numbers[3] if len(numbers) > 3 else 0.0
        yaw = numbers[4] if len(numbers) > 4 else 0.0
        roll = numbers[5] if len(numbers) > 5 else 0.0
        sx = numbers[6] if len(numbers) > 6 else 1.0
        sy = numbers[7] if len(numbers) > 7 else 1.0
        sz = numbers[8] if len(numbers) > 8 else 1.0
        print(format_json(scene_object_ops.create_object(self.client, name, mesh, x, y, z, pitch, yaw, roll, sx, sy, sz)))

    def do_despawn(self, arg: str) -> None:
        """despawn <name> -- remove a scene object by name"""
        args = _split_args(arg)
        if len(args) != 1:
            print("usage: despawn <name>")
            return
        print(format_json(scene_object_ops.remove_object(self.client, args[0])))

    def do_objects(self, arg: str) -> None:
        """objects -- list every actor placed in the current world (name, class, mesh if
        any, transform) -- see `listassets`/`listjsonassets` for Content Browser assets"""
        print(format_json(scene_object_ops.list_objects(self.client)))

    def do_moveobj(self, arg: str) -> None:
        """moveobj <name> <x> <y> <z> -- set an object's location (rotation/scale unchanged)"""
        args = _split_args(arg)
        if len(args) != 4:
            print("usage: moveobj <name> <x> <y> <z>")
            return
        name, x, y, z = args[0], float(args[1]), float(args[2]), float(args[3])
        print(format_json(transform_ops.set_transform(self.client, name, x=x, y=y, z=z)))

    def do_rotateobj(self, arg: str) -> None:
        """rotateobj <name> <pitch> <yaw> <roll> -- set an object's rotation (location/scale unchanged)"""
        args = _split_args(arg)
        if len(args) != 4:
            print("usage: rotateobj <name> <pitch> <yaw> <roll>")
            return
        name, pitch, yaw, roll = args[0], float(args[1]), float(args[2]), float(args[3])
        print(format_json(transform_ops.set_transform(self.client, name, pitch=pitch, yaw=yaw, roll=roll)))

    def do_scaleobj(self, arg: str) -> None:
        """scaleobj <name> <sx> <sy> <sz> -- set an object's scale (location/rotation unchanged)"""
        args = _split_args(arg)
        if len(args) != 4:
            print("usage: scaleobj <name> <sx> <sy> <sz>")
            return
        name, sx, sy, sz = args[0], float(args[1]), float(args[2]), float(args[3])
        print(format_json(transform_ops.set_transform(self.client, name, sx=sx, sy=sy, sz=sz)))

    def do_getobj(self, arg: str) -> None:
        """getobj <name> -- read an object's current transform"""
        args = _split_args(arg)
        if len(args) != 1:
            print("usage: getobj <name>")
            return
        print(format_json(transform_ops.get_transform(self.client, args[0])))

    def do_see(self, arg: str) -> None:
        """see <name> -- what the named observer currently sees, as compact JSON
        ({"n": name, "d": distance-m, "a": angle-deg, "box": [x,y,w,h]} per object)"""
        args = _split_args(arg)
        if len(args) != 1:
            print("usage: see <name>")
            return
        print(format_json(observer_ops.describe_observer(self.client, args[0])))

    def do_validate(self, arg: str) -> None:
        """validate <json-path> -- dry-run type/shape check from a file
        validate <json>      -- dry-run type/shape check from inline JSON"""
        arg = arg.strip()
        if arg.startswith("{"):
            payload = json.loads(arg)
        else:
            args = _split_args(arg)
            if len(args) != 1:
                print("usage: validate <json-path>  OR  validate <json>")
                return
            payload = json.loads(Path(args[0]).read_text(encoding="utf-8"))
        print(format_json(ops.validate_json_schema(self.client, payload)))

    def do_types(self, arg: str) -> None:
        """types -- list allowed field types and supported struct types"""
        print(format_json(ops.list_supported_types(self.client)))

    def do_status(self, arg: str) -> None:
        """status -- editor lifecycle status"""
        print(format_json(self.client.status()))

    def do_logs(self, arg: str) -> None:
        """logs [n] -- tail the last n lines of the editor log (default 50)"""
        lines = int(arg) if arg.strip() else 50
        print(self.client.tail_logs(lines))

    def do_stop(self, arg: str) -> None:
        """stop -- send a stop signal to the editor process"""
        self.client.stop()
        print("stop signal sent")

    def do_exit(self, arg: str) -> bool:
        """exit -- leave the shell"""
        return True

    do_quit = do_exit

    def do_EOF(self, arg: str) -> bool:
        if self.use_rawinput:
            print()
        return True
