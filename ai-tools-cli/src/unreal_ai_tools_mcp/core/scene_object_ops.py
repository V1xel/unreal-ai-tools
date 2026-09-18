"""Create/remove/list placed static mesh actors in the currently active world (editor or
PIE), identified by display name (the same name shown in the World Outliner). See
transform_ops.py for moving/rotating/scaling an existing one.
"""

from __future__ import annotations

from .editor_client import EditorClient


def create_object(
    client: EditorClient,
    name: str,
    mesh: str = "",
    x: float = 0.0,
    y: float = 0.0,
    z: float = 0.0,
    pitch: float = 0.0,
    yaw: float = 0.0,
    roll: float = 0.0,
    sx: float = 1.0,
    sy: float = 1.0,
    sz: float = 1.0,
) -> dict:
    """Upsert -- spawns a new object if none exists named `name`, else updates the existing
    one's mesh and transform. `mesh` defaults to the engine's basic cube if left empty."""
    return client.post_json(
        "/object/create",
        {
            "name": name,
            "mesh": mesh,
            "location": {"x": x, "y": y, "z": z},
            "rotation": {"pitch": pitch, "yaw": yaw, "roll": roll},
            "scale": {"x": sx, "y": sy, "z": sz},
        },
    )


def remove_object(client: EditorClient, name: str) -> dict:
    return client.post_json("/object/remove", {"name": name})


def list_objects(client: EditorClient) -> dict:
    return client.get_json("/object/list")
