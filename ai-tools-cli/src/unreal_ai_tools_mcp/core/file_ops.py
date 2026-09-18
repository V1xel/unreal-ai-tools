"""Move/delete for anything in the project, not just registered Unreal assets:

- A registered asset (a .uasset exists) -- moved/deleted through the editor
  (asset_ops), which fixes up every reference project-wide / refuses a delete
  that would leave dangling references.
- A JSON source file (see json_data_asset.py's Content/ convention) -- moved or
  deleted directly on disk, since it's just a file the editor doesn't know about.
- Anything else under Content/, or any file outside Content/ entirely -- a plain
  filesystem move/delete; Unreal has no concept of "asset" for it either way.

When a path resolves to a registered asset that also has a JSON source file (the
normal case for something created with `sync`/`create`), both are moved/deleted
together so they don't drift out of sync.
"""

from __future__ import annotations

import shutil
from pathlib import Path, PurePosixPath

from . import asset_ops
from .editor_client import EditorClient, EditorClientError


def _to_game_path(project: Path, raw: str) -> str | None:
    """Returns the canonical /Game/... path for `raw` (file or already a /Game/...
    string), or None if it isn't under the project's Content/ folder at all."""
    if raw.startswith("/Game/") or raw == "/Game":
        stripped = PurePosixPath(raw)
        if stripped.suffix in (".json", ".uasset"):
            return str(stripped.with_suffix(""))
        return raw.rstrip("/")

    file_path = Path(raw)
    if not file_path.is_absolute():
        file_path = Path.cwd() / file_path
    file_path = file_path.resolve()

    content_dir = (project.parent / "Content").resolve()
    try:
        relative = file_path.relative_to(content_dir)
    except ValueError:
        return None

    return "/Game/" + relative.with_suffix("").as_posix()


def _content_files(project: Path, game_path: str) -> tuple[Path, Path]:
    """Returns the (json, uasset) locations a /Game/... path would live at on disk
    (existence not implied -- callers check that)."""
    relative = game_path[len("/Game/") :]
    base = project.parent / "Content" / relative
    return base.with_suffix(".json"), base.with_suffix(".uasset")


def move(client: EditorClient, src: str, dst: str) -> dict:
    project = client.project
    src_game = _to_game_path(project, src)
    dst_game = _to_game_path(project, dst)

    if src_game is None or dst_game is None:
        src_path = Path(src).resolve()
        dst_path = Path(dst).resolve()
        if not src_path.is_file():
            raise EditorClientError(f"'{src_path}' does not exist")
        dst_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(src_path), str(dst_path))
        return {"movedFile": str(src_path), "to": str(dst_path)}

    src_json, src_uasset = _content_files(project, src_game)
    dst_json, dst_uasset = _content_files(project, dst_game)

    result: dict = {"from": src_game, "to": dst_game}

    if src_uasset.is_file():
        result["asset"] = asset_ops.move_asset(client, src_game, dst_game)

    if src_json.is_file():
        dst_json.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(src_json), str(dst_json))
        result["movedJsonSource"] = str(dst_json)

    if "asset" not in result and "movedJsonSource" not in result:
        # Neither a registered asset nor a JSON source at that location -- if the
        # caller gave a literal file (any other extension) under Content/, move
        # that file directly, preserving its extension.
        if not src.startswith("/Game/") and Path(src).resolve().is_file():
            src_path = Path(src).resolve()
            dst_path = dst_uasset.with_suffix(src_path.suffix)
            dst_path.parent.mkdir(parents=True, exist_ok=True)
            shutil.move(str(src_path), str(dst_path))
            result["movedFile"] = str(dst_path)
        else:
            raise EditorClientError(
                f"nothing found at '{src}' -- checked for a .uasset and a .json source under Content/"
            )

    return result


def delete(client: EditorClient, path: str, force: bool = False) -> dict:
    project = client.project
    game_path = _to_game_path(project, path)

    if game_path is None:
        file_path = Path(path).resolve()
        if not file_path.is_file():
            raise EditorClientError(f"'{file_path}' does not exist")
        file_path.unlink()
        return {"deletedFile": str(file_path)}

    json_file, uasset_file = _content_files(project, game_path)
    result: dict = {"path": game_path}

    if uasset_file.is_file():
        # Raises (with .data["referencedBy"]) if blocked -- deliberately left to
        # propagate so a refused asset delete doesn't still delete its JSON source.
        result["asset"] = asset_ops.delete_asset(client, game_path, force)

    if json_file.is_file():
        json_file.unlink()
        result["deletedJsonSource"] = str(json_file)

    if "asset" not in result and "deletedJsonSource" not in result:
        raise EditorClientError(
            f"nothing found at '{path}' -- checked for a .uasset and a .json source under Content/"
        )

    return result
