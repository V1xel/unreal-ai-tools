"""Generic asset operations -- move/delete for any Unreal asset, not just the
JSON-defined data assets in json_data_asset.py. See docs/architecture.md in the
toolkit repo.
"""

from __future__ import annotations

from .editor_client import EditorClient


def move_asset(client: EditorClient, old_path: str, new_path: str) -> dict:
    """Moves/renames an asset, fixing up every reference to it project-wide."""
    return client.post_json("/asset/move", {"from": old_path, "to": new_path})


def delete_asset(client: EditorClient, path: str, force: bool = False) -> dict:
    """Deletes an asset. Refuses (raising EditorClientError with `.data["referencedBy"]`
    set) if anything still references it, unless force is set."""
    return client.post_json("/asset/delete", {"path": path, "force": force})


def list_assets(client: EditorClient, folder: str | None = None, class_name: str | None = None) -> dict:
    """Lists any registered Unreal asset (StaticMesh, Texture, Material, JSON data assets,
    anything) -- not just placed scene objects or JSON data assets specifically. Optionally
    filtered by folder and/or class name (e.g. "StaticMesh")."""
    params = {k: v for k, v in {"folder": folder, "class": class_name}.items() if v}
    return client.get_json("/asset/list", params)
