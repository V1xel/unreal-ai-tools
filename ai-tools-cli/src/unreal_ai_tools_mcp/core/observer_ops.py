"""Observer cameras: named viewpoints placed in the level that report what they see as
compact JSON (name/distance/angle/bounding box per object) instead of pixels. Works both
while editing and during Play-In-Editor -- see docs/architecture.md.
"""

from __future__ import annotations

from .editor_client import EditorClient


def place_observer(
    client: EditorClient,
    name: str,
    x: float,
    y: float,
    z: float,
    pitch: float = 0.0,
    yaw: float = 0.0,
    roll: float = 0.0,
    fov: float = 90.0,
) -> dict:
    """Upsert -- creates the named observer if missing, else moves the existing one."""
    return client.post_json(
        "/observer/place",
        {
            "name": name,
            "location": {"x": x, "y": y, "z": z},
            "rotation": {"pitch": pitch, "yaw": yaw, "roll": roll},
            "fov": fov,
        },
    )


def list_observers(client: EditorClient) -> dict:
    """Lists observers placed in the currently active world (editor or PIE)."""
    return client.get_json("/observer/list")


def delete_observer(client: EditorClient, name: str) -> dict:
    return client.post_json("/observer/delete", {"name": name})


def describe_observer(client: EditorClient, name: str) -> dict:
    """What the named observer currently sees: {"seen": [{"n","d","a","box"}, ...]}."""
    return client.get_json("/observer/describe", {"name": name})
