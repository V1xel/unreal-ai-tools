"""Small helpers shared by the one-shot Typer commands and the interactive shell."""

from __future__ import annotations

import json as json_module
from pathlib import Path

from core.editor_client import EditorClientError


def resolve_asset_path(project: Path, json_path: Path) -> str:
    """Maps a JSON source file to its `/Game/...` asset path: JSON files mirror the
    project's Content/ layout 1:1, with `.json` in place of `.uasset`."""
    content_dir = (project.parent / "Content").resolve()
    json_path = json_path.resolve()
    try:
        relative = json_path.relative_to(content_dir)
    except ValueError as exc:
        raise EditorClientError(
            f"'{json_path}' is not under the project's Content folder ({content_dir}) -- "
            "JSON data-asset sources must live under Content/ so their path maps to a /Game/... asset path"
        ) from exc
    return "/Game/" + relative.with_suffix("").as_posix()


def asset_path_to_json_file(project: Path, asset_path: str) -> Path:
    """Inverse of resolve_asset_path: maps a `/Game/...` asset path to the .json file
    that would produce it, so inline `sync` can persist a source file to edit later."""
    if not asset_path.startswith("/Game/"):
        raise EditorClientError(f"'{asset_path}' must start with /Game/ to derive a Content/ file location")
    relative = asset_path[len("/Game/") :]
    return (project.parent / "Content" / relative).with_suffix(".json")


def format_json(data: dict) -> str:
    return json_module.dumps(data, indent=2)
