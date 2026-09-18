"""JSON-defined property-bag data asset operations.

Thin wrappers over the AITools plugin's HTTP routes (see docs/architecture.md and
docs/tools/json-data-assets.md in the toolkit repo). Each function here takes
already-resolved arguments (an asset path, parsed JSON) -- turning a local .json
file path into an asset path and reading it off disk is CLI-layer concern.
"""

from __future__ import annotations

from .editor_client import EditorClient


def sync_json_data_asset(client: EditorClient, path: str, json_payload: dict) -> dict:
    """Upsert -- create the .uasset at `path` if missing, else reconcile its fields."""
    return client.post_json("/data-asset/sync", {"path": path, "json": json_payload})


def read_data_asset_as_json(client: EditorClient, path: str) -> dict:
    """Reads an existing property-bag data asset back out as JSON."""
    return client.get_json("/data-asset/read", {"path": path})


def list_data_assets(client: EditorClient, folder: str | None = None, asset_type: str | None = None) -> dict:
    """Enumerates existing JSON-backed data assets, optionally filtered."""
    params = {k: v for k, v in {"folder": folder, "type": asset_type}.items() if v}
    return client.get_json("/data-asset/list", params)


def validate_json_schema(client: EditorClient, json_payload: dict) -> dict:
    """Dry-run type/shape check without touching any asset."""
    return client.post_json("/data-asset/validate", {"json": json_payload})


def list_supported_types(client: EditorClient) -> dict:
    """Lists allowed `type` values and supported engine struct types."""
    return client.get_json("/data-asset/types")


def find_referencing_data_assets(client: EditorClient, path: str) -> dict:
    """Finds JSON data assets whose fields reference `path` (hard or soft object/class
    references), via the asset registry's indexed dependency graph -- no per-asset
    iteration. Useful before moving/renaming/deleting `path` to see what would break."""
    return client.get_json("/data-asset/referencers", {"path": path})
