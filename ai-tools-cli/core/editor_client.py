"""Editor process lifecycle + HTTP client for the AITools plugin's server.

See docs/architecture.md ("Editor lifecycle") in the unreal-mcp-toolkit repo: the
toolkit owns starting/stopping/health-checking the editor transparently so the
data-asset operations never have to think about it, while the CLI also exposes
manual `uaicli editor start|stop|status|logs` commands for a human tester.
"""

from __future__ import annotations

import ctypes
import json
import os
import signal
import subprocess
import time
import winreg
from dataclasses import asdict, dataclass
from pathlib import Path

import httpx
import psutil

EDITOR_PROCESS_NAMES = {"unrealeditor.exe", "unrealeditor-cmd.exe"}

DEFAULT_PORT = 8760
HEALTH_CHECK_TIMEOUT = 2.0
START_TIMEOUT = 120.0

PROCESS_QUERY_LIMITED_INFORMATION = 0x1000


class EditorClientError(RuntimeError):
    """Raised for anything the CLI should report back as a clean error, not a traceback.

    `data` carries the parsed JSON error body when the editor returned one (e.g. a blocked
    delete's "referencedBy" list), so callers can show more than just the message."""

    def __init__(self, message: str, data: dict | None = None):
        super().__init__(message)
        self.data = data


def find_uproject(start: Path | None = None) -> Path:
    """Walks up from `start` (default: cwd) looking for a single *.uproject file."""
    env_project = os.environ.get("UAICLI_PROJECT")
    if env_project:
        path = Path(env_project).resolve()
        if not path.is_file():
            raise EditorClientError(f"UAICLI_PROJECT points to a missing file: {path}")
        return path

    current = (start or Path.cwd()).resolve()
    for directory in [current, *current.parents]:
        matches = list(directory.glob("*.uproject"))
        if len(matches) == 1:
            return matches[0]
        if len(matches) > 1:
            names = ", ".join(m.name for m in matches)
            raise EditorClientError(
                f"multiple .uproject files found in {directory}: {names} -- pass --project explicitly"
            )
    raise EditorClientError(
        "no .uproject file found in the current directory or any parent -- "
        "pass --project, or set UAICLI_PROJECT"
    )


def find_engine_dir(uproject: Path) -> Path:
    """Resolves the Engine install directory for `uproject`'s EngineAssociation."""
    env_engine = os.environ.get("UAICLI_ENGINE_DIR")
    if env_engine:
        return Path(env_engine)

    data = json.loads(uproject.read_text(encoding="utf-8"))
    association = data.get("EngineAssociation", "")
    if not association:
        raise EditorClientError(f"{uproject} has no EngineAssociation set")

    # Plain version numbers ("5.7") map to a standard Epic Games Launcher install.
    if association.replace(".", "").isdigit():
        candidate = Path(rf"C:\Program Files\Epic Games\UE_{association}")
        if candidate.is_dir():
            return candidate

    # Otherwise it's a build GUID registered by a source-built/custom engine install.
    for hive, subkey in (
        (winreg.HKEY_CURRENT_USER, r"SOFTWARE\Epic Games\Unreal Engine\Builds"),
        (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\EpicGames\Unreal Engine\Builds"),
    ):
        try:
            with winreg.OpenKey(hive, subkey) as key:
                value, _ = winreg.QueryValueEx(key, association)
                path = Path(value)
                if path.is_dir():
                    return path
        except FileNotFoundError:
            continue

    raise EditorClientError(
        f"could not resolve engine install for EngineAssociation '{association}' -- "
        "set UAICLI_ENGINE_DIR to the engine root (the folder containing 'Engine\\')"
    )


STILL_ACTIVE = 259


def _process_alive(pid: int) -> bool:
    """OpenProcess succeeding is not enough on its own -- it can still succeed briefly for
    an already-exited process, the same pitfall CPython's own Popen.__del__ guards against
    on Windows. Confirm via GetExitCodeProcess that it's actually still running."""
    handle = ctypes.windll.kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not handle:
        return False
    try:
        exit_code = ctypes.c_ulong()
        if not ctypes.windll.kernel32.GetExitCodeProcess(handle, ctypes.byref(exit_code)):
            return False
        return exit_code.value == STILL_ACTIVE
    finally:
        ctypes.windll.kernel32.CloseHandle(handle)


def is_pid_headless(pid: int) -> bool:
    """Whether the given editor process was launched with -nullrhi (no rendering, no
    window) -- see EditorClient.start(headless=True). Used to warn when a CLI session is
    controlling an editor instance that has nothing visible to look at."""
    try:
        cmdline = psutil.Process(pid).cmdline()
    except (psutil.NoSuchProcess, psutil.AccessDenied):
        return False
    return any(arg.lower() == "-nullrhi" for arg in cmdline)


def find_running_editor(project: Path) -> tuple[int, int] | None:
    """Scans running processes for an UnrealEditor(-Cmd).exe already hosting `project`,
    so the CLI can attach to an editor it didn't itself launch -- e.g. opened by
    double-clicking the .uproject, or from an IDE. Returns (pid, port), where port
    is read from a `-AIToolsPort=` argument if present, else DEFAULT_PORT."""
    project_path = str(project.resolve()).lower()
    project_name = project.name.lower()

    for proc in psutil.process_iter(["pid", "name", "cmdline"]):
        try:
            info = proc.info
            if (info.get("name") or "").lower() not in EDITOR_PROCESS_NAMES:
                continue

            cmdline = info.get("cmdline") or []
            if not any(arg.lower() == project_path or arg.lower().endswith(project_name) for arg in cmdline):
                continue

            port = DEFAULT_PORT
            for arg in cmdline:
                if arg.lower().startswith("-aitoolsport="):
                    try:
                        port = int(arg.split("=", 1)[1])
                    except ValueError:
                        pass
            return info["pid"], port
        except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
            continue

    return None


def list_running_editors() -> list[dict]:
    """Scans every running Unreal Editor process, regardless of project -- used so the CLI
    can auto-connect when exactly one is running, or list the options when several are,
    instead of always requiring --project."""
    results = []
    for proc in psutil.process_iter(["pid", "name", "cmdline"]):
        try:
            info = proc.info
            if (info.get("name") or "").lower() not in EDITOR_PROCESS_NAMES:
                continue

            cmdline = info.get("cmdline") or []
            project_path = next((Path(arg) for arg in cmdline if arg.lower().endswith(".uproject")), None)
            if project_path is None:
                continue

            port = DEFAULT_PORT
            headless = False
            for arg in cmdline:
                if arg.lower().startswith("-aitoolsport="):
                    try:
                        port = int(arg.split("=", 1)[1])
                    except ValueError:
                        pass
                if arg.lower() == "-nullrhi":
                    headless = True

            results.append({"pid": info["pid"], "project": project_path, "port": port, "headless": headless})
        except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
            continue

    return results


def discover_project(explicit: Path | None = None) -> Path:
    """Resolves which .uproject to talk to, in priority order: an explicit --project, then
    UAICLI_PROJECT, then whichever editor is already running (auto-selected if there's
    exactly one; raises listing the options if there are several so the caller can pick
    with --project), then the usual directory-walk (find_uproject) if nothing is running."""
    if explicit:
        path = explicit.resolve()
        if not path.is_file():
            raise EditorClientError(f"project file not found: {path}")
        return path

    if os.environ.get("UAICLI_PROJECT"):
        return find_uproject()

    running = list_running_editors()
    if len(running) == 1:
        return running[0]["project"].resolve()
    if len(running) > 1:
        options = "\n".join(
            f"  - {r['project']}  (pid {r['pid']}, port {r['port']}{', headless' if r['headless'] else ''})"
            for r in running
        )
        raise EditorClientError(f"multiple running editors found -- pass --project to pick one:\n{options}")

    return find_uproject()


@dataclass
class EditorState:
    pid: int
    port: int
    log_path: str
    started_at: float


class EditorClient:
    """Owns one Unreal project's editor lifecycle and talks to its AITools HTTP server."""

    def __init__(self, project: Path | None = None, port: int | None = None):
        self.project = discover_project(project)
        self._port_explicit = port is not None
        self.port = port if port is not None else DEFAULT_PORT
        self.base_url = f"http://127.0.0.1:{self.port}"
        self._saved_dir = self.project.parent / "Saved" / "AITools"
        self._state_path = self._saved_dir / "editor_state.json"
        self._log_path = self._saved_dir / "editor.log"
        self._http = httpx.Client(base_url=self.base_url, timeout=30.0)

    def close(self) -> None:
        self._http.close()

    def __enter__(self) -> EditorClient:
        return self

    def __exit__(self, *_exc: object) -> None:
        self.close()

    # ---- health / lifecycle -------------------------------------------------

    def is_running(self) -> bool:
        try:
            response = self._http.get("/health", timeout=HEALTH_CHECK_TIMEOUT)
            return response.status_code == 200
        except httpx.HTTPError:
            return False

    def _rebind_port(self, port: int) -> None:
        if port == self.port:
            return
        self.port = port
        self.base_url = f"http://127.0.0.1:{self.port}"
        self._http.close()
        self._http = httpx.Client(base_url=self.base_url, timeout=30.0)

    def discover(self) -> bool:
        """Finds an already-running editor for this project -- started outside the
        CLI -- and binds to its port. Returns whether the editor is now reachable."""
        if self.is_running():
            return True

        found = find_running_editor(self.project)
        if not found:
            return False

        _pid, port = found
        if not self._port_explicit:
            self._rebind_port(port)
        return self.is_running()

    def _read_state(self) -> EditorState | None:
        if not self._state_path.is_file():
            return None
        try:
            return EditorState(**json.loads(self._state_path.read_text(encoding="utf-8")))
        except (ValueError, TypeError, KeyError):
            return None

    def _write_state(self, state: EditorState) -> None:
        self._saved_dir.mkdir(parents=True, exist_ok=True)
        self._state_path.write_text(json.dumps(asdict(state)), encoding="utf-8")

    def start(self, *, headless: bool = False, wait: bool = True) -> None:
        if self.discover():
            raise EditorClientError(f"an editor is already listening on {self.base_url}")

        engine_dir = find_engine_dir(self.project)
        editor_exe = engine_dir / "Engine" / "Binaries" / "Win64" / "UnrealEditor.exe"
        if not editor_exe.is_file():
            raise EditorClientError(f"UnrealEditor.exe not found at {editor_exe}")

        self._saved_dir.mkdir(parents=True, exist_ok=True)
        args = [str(editor_exe), str(self.project), f"-AIToolsPort={self.port}"]
        if headless:
            args += ["-unattended", "-nullrhi", "-nosplash"]

        creationflags = subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP
        with open(self._log_path, "wb") as log_file:
            process = subprocess.Popen(
                args,
                stdout=log_file,
                stderr=subprocess.STDOUT,
                creationflags=creationflags,
            )
            # We detach immediately and track liveness by PID (see _process_alive), never
            # through this Popen object again -- prevent its __del__ from polling a
            # DETACHED_PROCESS handle at GC time (harmless but noisy "handle is invalid").
            process.returncode = 0

        self._write_state(
            EditorState(pid=process.pid, port=self.port, log_path=str(self._log_path), started_at=time.time())
        )

        if wait:
            self.wait_until_ready()

    def wait_until_ready(self, timeout: float = START_TIMEOUT) -> None:
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.is_running():
                return
            time.sleep(1.0)
        raise EditorClientError(
            f"editor did not become ready on {self.base_url} within {timeout:.0f}s -- check the log at {self._log_path}"
        )

    def ensure_running(self, *, headless: bool = False) -> None:
        """Called transparently before any data-asset operation (see architecture.md)."""
        if self.discover():
            return

        state = self._read_state()
        if state and _process_alive(state.pid):
            # Process is alive but not answering yet (still booting) -- just wait.
            self.wait_until_ready()
            return

        self.start(headless=headless)

    def _find_pid(self) -> int | None:
        state = self._read_state()
        if state and _process_alive(state.pid):
            return state.pid
        found = find_running_editor(self.project)
        return found[0] if found else None

    def stop(self) -> None:
        pid = self._find_pid()
        if pid is None:
            raise EditorClientError("no running editor found for this project")
        os.kill(pid, signal.SIGTERM)  # Windows maps SIGTERM to TerminateProcess

    def status(self) -> dict:
        self.discover()
        pid = self._find_pid()
        return {
            "running": self.is_running(),
            "base_url": self.base_url,
            "pid": pid,
            "headless": is_pid_headless(pid) if pid is not None else None,
            "log_path": str(self._log_path),
        }

    def pid_and_headless(self) -> tuple[int | None, bool]:
        """For UX only (e.g. the shell prompt) -- which editor process is being controlled,
        and whether it's headless (-nullrhi, nothing visible to look at)."""
        self.discover()
        pid = self._find_pid()
        return pid, (is_pid_headless(pid) if pid is not None else False)

    def tail_logs(self, lines: int = 50) -> str:
        if not self._log_path.is_file():
            return "(no log file yet)"
        text = self._log_path.read_text(encoding="utf-8", errors="replace")
        return "\n".join(text.splitlines()[-lines:])

    # ---- generic HTTP helpers used by core/json_data_asset.py ---------------

    def get_json(self, path: str, params: dict | None = None) -> dict:
        self.ensure_running()
        response = self._http.get(path, params=params)
        return _unwrap(response)

    def post_json(self, path: str, body: dict) -> dict:
        self.ensure_running()
        response = self._http.post(path, json=body)
        return _unwrap(response)


def _unwrap(response: httpx.Response) -> dict:
    try:
        data = response.json()
    except ValueError as exc:
        raise EditorClientError(
            f"non-JSON response ({response.status_code}) from the editor: {response.text[:500]}"
        ) from exc

    if response.status_code >= 400:
        raise EditorClientError(data.get("error", f"request failed with status {response.status_code}"), data=data)
    return data
