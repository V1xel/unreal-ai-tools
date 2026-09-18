"""Move/rotate/scale an existing actor in the currently active world (editor or PIE),
identified by display name. See scene_object_ops.py for creating/removing one. Any
argument left as None is unchanged on the actor.
"""

from __future__ import annotations

from .editor_client import EditorClient


def set_transform(
    client: EditorClient,
    name: str,
    x: float | None = None,
    y: float | None = None,
    z: float | None = None,
    pitch: float | None = None,
    yaw: float | None = None,
    roll: float | None = None,
    sx: float | None = None,
    sy: float | None = None,
    sz: float | None = None,
) -> dict:
    body: dict = {"name": name}
    if x is not None and y is not None and z is not None:
        body["location"] = {"x": x, "y": y, "z": z}
    if pitch is not None and yaw is not None and roll is not None:
        body["rotation"] = {"pitch": pitch, "yaw": yaw, "roll": roll}
    if sx is not None and sy is not None and sz is not None:
        body["scale"] = {"x": sx, "y": sy, "z": sz}
    return client.post_json("/object/transform", body)


def get_transform(client: EditorClient, name: str) -> dict:
    return client.get_json("/object/get", {"name": name})
