#!/usr/bin/env python3
"""
BareMRI MCP Server — bridges MCP tool calls to BareMRI's TCP command interface.

The agent discovers NIfTI files with baremri_list_files, then opens them
for human inspection with baremri_open. All viewer controls work after open.
"""

import asyncio
import json
import os
import shutil
import sys
from pathlib import Path

from mcp.server import Server
from mcp.server.models import InitializationOptions
from mcp.server.stdio import stdio_server
from mcp.types import Tool, TextContent, ToolsCapability, ServerCapabilities

# ── config ──────────────────────────────────────────────────────────

BAREMRI_DIR = Path(__file__).resolve().parent.parent
BAREMRI_BIN = BAREMRI_DIR / "baremri"

# Priority list: first binary found on PATH wins, falls back to project build
def _find_baremri() -> Path:
    """Resolve baremri binary: system PATH first, then project build."""
    system = shutil.which("baremri")
    if system:
        return Path(system)
    project = BAREMRI_DIR / "baremri"
    if project.is_file():
        return project
    raise RuntimeError(
        "baremri not found on PATH and not built in project. "
        f"Run 'make PROFILE=docs/superpowers/profiles/mcp-test.profile' in {BAREMRI_DIR} first."
    )

NIFTI_GLOBS = ["*.nii", "*.nii.gz"]


# ── file discovery ──────────────────────────────────────────────────

def list_nifti_files(directory: str) -> list[str]:
    """Return sorted list of .nii / .nii.gz files under directory."""
    root = Path(directory).resolve()
    if not root.is_dir():
        return []
    files: set[str] = set()
    for pattern in NIFTI_GLOBS:
        for p in root.rglob(pattern):
            if p.is_file() and not p.is_symlink():
                files.add(str(p))
    return sorted(files)


# ── BareMRI process management ──────────────────────────────────────

class BareMRIProcess:
    """Manages the BareMRI subprocess and TCP connection."""

    def __init__(self):
        self.proc = None
        self.reader: asyncio.StreamReader | None = None
        self.writer: asyncio.StreamWriter | None = None
        self._lock = asyncio.Lock()
        self._built = False
        self._bin = _find_baremri()  # resolved on import: system PATH or project

    @property
    def is_running(self) -> bool:
        return self.proc is not None and self.proc.returncode is None

    async def _build(self):
        """Build BareMRI with MCP plugin (once per session).
        Skipped if baremri found on PATH — assume system binary already has MCP."""
        if self._built:
            return
        # Only build if we're using the project binary
        if self._bin != (BAREMRI_DIR / "baremri"):
            self._built = True
            return
        build = await asyncio.create_subprocess_exec(
            "make", "PROFILE=docs/superpowers/profiles/mcp-test.profile", "-C", str(BAREMRI_DIR),
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
        stdout, stderr = await build.communicate()
        if build.returncode != 0:
            raise RuntimeError(f"make failed:\n{stderr.decode()}")
        self._built = True

    async def shutdown(self):
        """Close connection and terminate BareMRI."""
        if self.writer:
            try:
                self.writer.close()
                await self.writer.wait_closed()
            except Exception:
                pass
            self.writer = None
            self.reader = None
        if self.proc:
            try:
                self.proc.terminate()
                await self.proc.wait()
            except Exception:
                pass
            self.proc = None

    async def launch(self, files: list[str], **kwargs) -> dict:
        """Build (if needed), shut down old instance, launch with files."""
        async with self._lock:
            await self.shutdown()
            await self._build()

            # Resolve file paths relative to BAREMRI_DIR (project root),
            # since the subprocess inherits our CWD which may be mcp/.
            resolved_files = []
            for f in files:
                p = Path(f)
                if not p.is_absolute():
                    p = (BAREMRI_DIR / p).resolve()
                resolved_files.append(str(p))

            # extra args from kwargs: seg, overlay, out, events, profile
            extra = []
            for key, flag in [("seg", "--seg"), ("overlay", "--overlay"),
                              ("out", "--out"), ("events", "--events"),
                              ("profile", "--profile")]:
                if key in kwargs and kwargs[key]:
                    val = str(kwargs[key])
                    # resolve relative paths for seg/overlay too
                    if key in ("seg", "overlay", "out", "events"):
                        p = Path(val)
                        if not p.is_absolute():
                            val = str((BAREMRI_DIR / p).resolve())
                    extra.append(flag)
                    extra.append(val)
            extra.extend(resolved_files)

            args = [str(self._bin)] + extra
            self.proc = await asyncio.create_subprocess_exec(
                *args,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE)

            # read the ephemeral port from stdout
            port = None
            for _ in range(200):  # ~10 second timeout
                try:
                    line = await asyncio.wait_for(
                        self.proc.stdout.readline(), timeout=0.05)
                except asyncio.TimeoutError:
                    continue
                line = line.decode().strip()
                if line.startswith("MCP_PORT="):
                    port = int(line.split("=", 1)[1])
                    break
                if not line:
                    break
            if port is None:
                await self.shutdown()
                raise RuntimeError("did not get MCP_PORT from BareMRI stdout")

            # connect TCP
            self.reader, self.writer = await asyncio.open_connection(
                "127.0.0.1", port)
            print(f"MCP: opened BareMRI with {len(files)} file(s) on port {port}",
                  file=sys.stderr)
            return {"ok": True, "port": port, "files": files}

    async def send_command(self, cmd: str, **kwargs) -> dict:
        """Send a command to BareMRI, return parsed response."""
        async with self._lock:
            if not self.is_running:
                return {"ok": False,
                        "error": "BareMRI not open — call baremri_open first"}
            if not self.writer:
                return {"ok": False, "error": "no connection"}
            payload = {"cmd": cmd, **kwargs}
            line = json.dumps(payload, separators=(",", ":")) + "\n"
            self.writer.write(line.encode())
            await self.writer.drain()
            response_line = await asyncio.wait_for(
                self.reader.readline(), timeout=5.0)
            return json.loads(response_line.decode().strip())


# ── MCP server ──────────────────────────────────────────────────────

bare = BareMRIProcess()
server = Server("baremri")

# tool definitions
TOOLS = [
    # ── session management ──
    Tool(name="baremri_list_files",
         description="List all .nii and .nii.gz files under a directory. Use this to discover available NIfTI files before opening them.",
         inputSchema={"type": "object", "properties": {
             "directory": {"type": "string",
                          "description": "Directory to search (defaults to current working directory)"}},
             "required": []}),
    Tool(name="baremri_open",
         description="Open NIfTI files in BareMRI for the human to inspect. Supports optional flags. Ask the human which profile they prefer before opening.",
         inputSchema={"type": "object", "properties": {
             "files": {"type": "array", "items": {"type": "string"},
                      "description": "List of .nii / .nii.gz file paths to open"},
             "seg": {"type": "string",
                     "description": "Optional segmentation file"},
             "overlay": {"type": "string",
                         "description": "Optional overlay file"},
             "out": {"type": "string",
                     "description": "Optional screenshot output directory"},
             "events": {"type": "string",
                        "description": "Optional events TSV file"},
             "profile": {"type": "string",
                         "enum": ["lt", "laptop", "t", "tall", "m", "medium", "l", "large"],
                         "description": "Window size: laptop/lt (400x830), tall/t (800x970), medium/m (1200x630 default), large/l (1600x900). Custom WxH also accepted (e.g. 1024x768). Ask the human which they prefer."}},
             "required": ["files"]}),
    # ── navigation ──
    Tool(name="baremri_set_crosshair",
         description="Move the crosshair to voxel coordinates (x,y,z). Coords are clamped to image bounds.",
         inputSchema={"type": "object", "properties": {
             "x": {"type": "integer"}, "y": {"type": "integer"},
             "z": {"type": "integer"}},
             "required": ["x", "y", "z"]}),
    Tool(name="baremri_set_focus",
         description="Switch the focused view.",
         inputSchema={"type": "object", "properties": {
             "view": {"type": "string",
                      "enum": ["axial", "sagittal", "coronal"]}},
             "required": ["view"]}),
    Tool(name="baremri_navigate_slice",
         description="Advance the slice in the focused view by +1 or -1.",
         inputSchema={"type": "object", "properties": {
             "delta": {"type": "integer"}}, "required": ["delta"]}),
    Tool(name="baremri_switch_image",
         description="Switch to a different loaded image by index (0-based).",
         inputSchema={"type": "object", "properties": {
             "index": {"type": "integer"}}, "required": ["index"]}),
    # ── view control ──
    Tool(name="baremri_set_zoom",
         description="Set zoom level (1.0 = fit, max 32.0). Returns error if out of range.",
         inputSchema={"type": "object", "properties": {
             "zoom": {"type": "number"}}, "required": ["zoom"]}),
    Tool(name="baremri_set_colormap",
         description="Set the colormap for the active image.",
         inputSchema={"type": "object", "properties": {
             "name": {"type": "string",
                      "enum": ["gray", "heat", "turbo", "viridis", "inferno"]}},
             "required": ["name"]}),
    Tool(name="baremri_set_window",
         description="Set contrast window width.",
         inputSchema={"type": "object", "properties": {
             "width": {"type": "number"}}, "required": ["width"]}),
    Tool(name="baremri_set_level",
         description="Set contrast level (center value).",
         inputSchema={"type": "object", "properties": {
             "level": {"type": "number"}}, "required": ["level"]}),
    Tool(name="baremri_reset_contrast",
         description="Reset window/level to auto-computed range.",
         inputSchema={"type": "object", "properties": {}}),
    # ── time series (4D) ──
    Tool(name="baremri_set_timepoint",
         description="Jump to a specific time point (4D data only).",
         inputSchema={"type": "object", "properties": {
             "t": {"type": "integer"}}, "required": ["t"]}),
    Tool(name="baremri_step_timepoint",
         description="Step forward or backward one time point. Wraps. Use direction: -1 or 1.",
         inputSchema={"type": "object", "properties": {
             "direction": {"type": "integer", "enum": [-1, 1]}},
             "required": ["direction"]}),
    # ── overlay / segmentation ──
    Tool(name="baremri_toggle_overlay",
         description="Show or hide the overlay.",
         inputSchema={"type": "object", "properties": {
             "show": {"type": "boolean"}}, "required": ["show"]}),
    Tool(name="baremri_set_overlay_threshold",
         description="Set overlay threshold (0.25–20.0). Returns error if out of range.",
         inputSchema={"type": "object", "properties": {
             "value": {"type": "number"}}, "required": ["value"]}),
    Tool(name="baremri_set_overlay_opacity",
         description="Set overlay opacity (0.0–1.0).",
         inputSchema={"type": "object", "properties": {
             "value": {"type": "number"}}, "required": ["value"]}),
    Tool(name="baremri_set_seg_opacity",
         description="Set segmentation overlay opacity (0.0–1.0).",
         inputSchema={"type": "object", "properties": {
             "value": {"type": "number"}}, "required": ["value"]}),
    # ── output ──
    Tool(name="baremri_screenshot",
         description="Save a BMP screenshot of the focused view.",
         inputSchema={"type": "object", "properties": {
             "seg": {"type": "boolean", "default": False}}}),
    # ── state ──
    Tool(name="baremri_get_state",
         description="Call this first. Returns complete viewer state: loaded images (filenames, dimensions, 4D timepoints, TR), current crosshair, focus view, zoom, colormap, contrast window/level, overlay/segmentation status, and event markers. Use this to discover what modes are available before sending commands.",
         inputSchema={"type": "object", "properties": {}}),
]


@server.list_tools()
async def handle_list_tools() -> list[Tool]:
    return TOOLS


@server.call_tool()
async def handle_call_tool(name: str, arguments: dict) -> list[TextContent]:
    try:
        if name == "baremri_list_files":
            directory = arguments.get("directory", str(Path.cwd()))
            files = list_nifti_files(directory)
            return [TextContent(type="text",
                    text=json.dumps({"ok": True, "files": files,
                                     "count": len(files)}, indent=2))]

        if name == "baremri_open":
            files = arguments.get("files", [])
            if not files:
                return [TextContent(type="text",
                        text=json.dumps({"ok": False,
                                         "error": "files list is empty"}))]
            resp = await bare.launch(
                files,
                seg=arguments.get("seg"),
                overlay=arguments.get("overlay"),
                out=arguments.get("out"),
                events=arguments.get("events"),
                profile=arguments.get("profile"),
            )
            return [TextContent(type="text", text=json.dumps(resp, indent=2))]

        # all other tools → forward to BareMRI TCP
        wire_cmd = name.replace("baremri_", "", 1)
        resp = await bare.send_command(wire_cmd, **arguments)
        return [TextContent(type="text", text=json.dumps(resp, indent=2))]

    except Exception as e:
        return [TextContent(type="text",
                           text=json.dumps({"ok": False, "error": str(e)}))]


async def main():
    async with stdio_server() as (read_stream, write_stream):
        try:
            await server.run(
                read_stream, write_stream,
                InitializationOptions(
                    server_name="baremri",
                    server_version="0.1.0",
                    capabilities=ServerCapabilities(
                        tools=ToolsCapability(),
                    ),
                ))
        finally:
            await bare.shutdown()


if __name__ == "__main__":
    asyncio.run(main())
