"""Typer entrypoint -- `umcp`. Calls into core/, never talks HTTP directly.

Command tree mirrors docs/tools/json-data-assets.md ("Tool / CLI surface") in the
toolkit repo:

    umcp editor start|stop|status|logs
    umcp data-asset sync <json-path>
    umcp data-asset read <asset-path> [--out file.json]
    umcp data-asset list [--folder] [--type]
    umcp data-asset validate <json-path>
    umcp data-asset types
    umcp asset move <src> <dst>                 # any asset/JSON source/file, reference-safe
    umcp asset delete <path> [--force]
    umcp observer place <name> <x> <y> <z>      # named viewpoint, editor + PIE
    umcp observer describe <name>               # what it sees, as compact JSON
    umcp object create <name> <x> <y> <z>       # spawn/move a scene object (defaults to a cube)
    umcp object remove <name>
    umcp transform set <name> [--x --y --z] [--pitch --yaw --roll] [--sx --sy --sz]
    umcp shell                                  # interactive: connect once, run many commands

"""

from __future__ import annotations

import json as json_module
import sys
from pathlib import Path

import questionary
import typer

from unreal_ai_tools_mcp.core import asset_ops
from unreal_ai_tools_mcp.core import file_ops
from unreal_ai_tools_mcp.core import json_data_asset as ops
from unreal_ai_tools_mcp.core import observer_ops
from unreal_ai_tools_mcp.core import scene_object_ops
from unreal_ai_tools_mcp.core import transform_ops
from unreal_ai_tools_mcp.core.editor_client import DEFAULT_PORT, EditorClient, EditorClientError, list_running_editors

from .common import format_json, resolve_asset_path
from .shell import InteractiveShell

app = typer.Typer(help="CLI front-end for the AITools Unreal Engine plugin.")
editor_app = typer.Typer(help="Manual editor lifecycle controls.")
data_asset_app = typer.Typer(help="JSON-defined property-bag data assets.")
asset_app = typer.Typer(help="Generic move/delete for any asset, JSON source, or file.")
observer_app = typer.Typer(help="Observer cameras -- named viewpoints reporting what they see as JSON.")
object_app = typer.Typer(help="Create/remove/list placed scene objects.")
transform_app = typer.Typer(help="Move/rotate/scale an existing scene object.")
app.add_typer(editor_app, name="editor")
app.add_typer(data_asset_app, name="data-asset")
app.add_typer(asset_app, name="asset")
app.add_typer(observer_app, name="observer")
app.add_typer(object_app, name="object")
app.add_typer(transform_app, name="transform")


@app.callback()
def root(
    ctx: typer.Context,
    project: Path | None = typer.Option(
        None,
        "--project",
        help="Path to the .uproject file. Default: auto-detect from running editor processes "
        "(errors listing options if more than one is running), else auto-discover from the current directory.",
    ),
    port: int | None = typer.Option(
        None, "--port", help=f"AITools HTTP server port (default: {DEFAULT_PORT}, or auto-detected from a running editor)"
    ),
) -> None:
    if project is None and sys.stdin.isatty():
        running = list_running_editors()
        if len(running) > 1:
            choice = questionary.select(
                "Multiple Unreal editors are running -- pick one:",
                choices=[
                    questionary.Choice(
                        title=f"{r['project']}  (pid {r['pid']}, port {r['port']}{', headless' if r['headless'] else ''})",
                        value=r,
                    )
                    for r in running
                ],
            ).ask()
            if choice is None:
                raise typer.Exit(code=1)
            project = choice["project"]
            if port is None:
                port = choice["port"]

    ctx.obj = {"project": project, "port": port}


def _client(ctx: typer.Context) -> EditorClient:
    return EditorClient(project=ctx.obj["project"], port=ctx.obj["port"])


def _print_json(data: dict) -> None:
    typer.echo(format_json(data))


@editor_app.command("start")
def editor_start(
    ctx: typer.Context,
    headless: bool = typer.Option(False, help="Launch without a GUI (-unattended -nullrhi -nosplash)"),
) -> None:
    client = _client(ctx)
    client.start(headless=headless)
    typer.echo(f"editor listening on {client.base_url}")


@editor_app.command("stop")
def editor_stop(ctx: typer.Context) -> None:
    client = _client(ctx)
    client.stop()
    typer.echo("stop signal sent")


@editor_app.command("status")
def editor_status(ctx: typer.Context) -> None:
    _print_json(_client(ctx).status())


@editor_app.command("logs")
def editor_logs(ctx: typer.Context, lines: int = typer.Option(50, "--lines", "-n")) -> None:
    typer.echo(_client(ctx).tail_logs(lines))


@data_asset_app.command("sync")
def data_asset_sync(ctx: typer.Context, json_path: Path) -> None:
    client = _client(ctx)
    payload = json_module.loads(json_path.read_text(encoding="utf-8"))
    asset_path = resolve_asset_path(client.project, json_path)
    _print_json(ops.sync_json_data_asset(client, asset_path, payload))


@data_asset_app.command("read")
def data_asset_read(
    ctx: typer.Context,
    asset_path: str,
    out: Path | None = typer.Option(None, "--out", help="Write the JSON to this file instead of stdout"),
) -> None:
    client = _client(ctx)
    result = ops.read_data_asset_as_json(client, asset_path)
    text = json_module.dumps(result, indent=2)
    if out:
        out.write_text(text, encoding="utf-8")
    else:
        typer.echo(text)


@data_asset_app.command("list")
def data_asset_list(
    ctx: typer.Context,
    folder: str | None = typer.Option(None, "--folder"),
    type: str | None = typer.Option(None, "--type"),
) -> None:
    _print_json(ops.list_data_assets(_client(ctx), folder, type))


@data_asset_app.command("referencers")
def data_asset_referencers(ctx: typer.Context, asset_path: str) -> None:
    """JSON data assets referencing asset_path -- an indexed asset-registry lookup, not a
    scan of every data asset. Useful before moving/renaming/deleting asset_path."""
    _print_json(ops.find_referencing_data_assets(_client(ctx), asset_path))


@data_asset_app.command("validate")
def data_asset_validate(ctx: typer.Context, json_path: Path) -> None:
    client = _client(ctx)
    payload = json_module.loads(json_path.read_text(encoding="utf-8"))
    result = ops.validate_json_schema(client, payload)
    _print_json(result)
    if not result.get("valid", False):
        raise typer.Exit(code=1)


@data_asset_app.command("types")
def data_asset_types(ctx: typer.Context) -> None:
    _print_json(ops.list_supported_types(_client(ctx)))


@asset_app.command("move")
def asset_move(ctx: typer.Context, src: str, dst: str) -> None:
    """Move/rename anything: a registered asset (fixes up every reference project-wide),
    a JSON source file, or any other file. A .uasset's .json source moves with it."""
    _print_json(file_ops.move(_client(ctx), src, dst))


asset_app.command("rename")(asset_move)


@asset_app.command("delete")
def asset_delete(
    ctx: typer.Context,
    path: str,
    force: bool = typer.Option(False, help="Delete even if other assets still reference it"),
) -> None:
    """Delete anything: a registered asset (refused if still referenced, unless --force),
    a JSON source file, or any other file."""
    _print_json(file_ops.delete(_client(ctx), path, force))


@asset_app.command("list")
def asset_list(
    ctx: typer.Context,
    folder: str | None = typer.Option(None, "--folder"),
    class_name: str | None = typer.Option(None, "--class"),
) -> None:
    """List any registered Unreal asset (StaticMesh, Texture, Material, JSON data assets,
    anything), optionally filtered by folder and/or class. See `data-asset list` for JSON
    data assets specifically, `object list` for actors placed in the scene."""
    _print_json(asset_ops.list_assets(_client(ctx), folder, class_name))


@observer_app.command("place")
def observer_place(
    ctx: typer.Context,
    name: str,
    x: float,
    y: float,
    z: float,
    pitch: float = typer.Option(0.0, "--pitch"),
    yaw: float = typer.Option(0.0, "--yaw"),
    roll: float = typer.Option(0.0, "--roll"),
    fov: float = typer.Option(90.0, "--fov"),
) -> None:
    """Place/move a named observer camera (upsert). Works in the editor and during PIE."""
    _print_json(observer_ops.place_observer(_client(ctx), name, x, y, z, pitch, yaw, roll, fov))


@observer_app.command("list")
def observer_list(ctx: typer.Context) -> None:
    _print_json(observer_ops.list_observers(_client(ctx)))


@observer_app.command("delete")
def observer_delete(ctx: typer.Context, name: str) -> None:
    _print_json(observer_ops.delete_observer(_client(ctx), name))


@observer_app.command("describe")
def observer_describe(ctx: typer.Context, name: str) -> None:
    """What the named observer currently sees, as compact JSON."""
    _print_json(observer_ops.describe_observer(_client(ctx), name))


@object_app.command("create")
def object_create(
    ctx: typer.Context,
    name: str,
    x: float,
    y: float,
    z: float,
    mesh: str = typer.Option("", "--mesh", help="Static mesh asset path (default: engine basic cube)"),
    pitch: float = typer.Option(0.0, "--pitch"),
    yaw: float = typer.Option(0.0, "--yaw"),
    roll: float = typer.Option(0.0, "--roll"),
    sx: float = typer.Option(1.0, "--sx"),
    sy: float = typer.Option(1.0, "--sy"),
    sz: float = typer.Option(1.0, "--sz"),
) -> None:
    """Create/move a static mesh object (upsert)."""
    _print_json(scene_object_ops.create_object(_client(ctx), name, mesh, x, y, z, pitch, yaw, roll, sx, sy, sz))


@object_app.command("remove")
def object_remove(ctx: typer.Context, name: str) -> None:
    _print_json(scene_object_ops.remove_object(_client(ctx), name))


@object_app.command("list")
def object_list(ctx: typer.Context) -> None:
    _print_json(scene_object_ops.list_objects(_client(ctx)))


@transform_app.command("set")
def transform_set(
    ctx: typer.Context,
    name: str,
    x: float | None = typer.Option(None, "--x"),
    y: float | None = typer.Option(None, "--y"),
    z: float | None = typer.Option(None, "--z"),
    pitch: float | None = typer.Option(None, "--pitch"),
    yaw: float | None = typer.Option(None, "--yaw"),
    roll: float | None = typer.Option(None, "--roll"),
    sx: float | None = typer.Option(None, "--sx"),
    sy: float | None = typer.Option(None, "--sy"),
    sz: float | None = typer.Option(None, "--sz"),
) -> None:
    """Move/rotate/scale an existing object by name. Only the groups you provide
    (location x/y/z, rotation pitch/yaw/roll, scale sx/sy/sz) are changed."""
    _print_json(transform_ops.set_transform(_client(ctx), name, x, y, z, pitch, yaw, roll, sx, sy, sz))


@transform_app.command("get")
def transform_get(ctx: typer.Context, name: str) -> None:
    _print_json(transform_ops.get_transform(_client(ctx), name))


@app.command("shell")
def shell(ctx: typer.Context) -> None:
    """Interactive mode: connect once, then run editor/data-asset commands from stdin
    (one per line) without repeating --project/--port. See `help` inside the shell."""
    with _client(ctx) as client:
        InteractiveShell(client).cmdloop()


def main() -> None:
    try:
        app()
    except EditorClientError as exc:
        typer.echo(f"error: {exc}", err=True)
        if exc.data and exc.data.get("referencedBy"):
            typer.echo("referenced by:", err=True)
            for referencer in exc.data["referencedBy"]:
                typer.echo(f"  {referencer}", err=True)
        raise SystemExit(1) from exc


if __name__ == "__main__":
    main()
