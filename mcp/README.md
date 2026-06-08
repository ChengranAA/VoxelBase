# BareMRI MCP Server

MCP server for BareMRI — lets Claude and other AI agents drive the NIfTI viewer
while you watch in the loop.

## Setup

```sh
cd mcp
uv sync
```

This creates a `.venv` with the MCP SDK and all dependencies.

## Usage

Add to your MCP client config (Claude Desktop, etc.):

```json
{
  "mcpServers": {
    "baremri": {
      "command": "uv",
      "args": [
        "run", "--directory", "/path/to/BareMRI/mcp", "server.py"
      ],
      "env": {}
    }
  }
}
```

The server starts without opening any files. The agent discovers what's
available with `baremri_list_files`, then opens them for inspection:

```
1. Agent: baremri_list_files("/data")  →  ["anat.nii", "func.nii", ...]
2. Agent: baremri_open(["anat.nii", "func.nii"])  →  launches viewer
3. Human: inspects the images
4. Agent: baremri_set_crosshair(90, 110, 50), navigate, screenshot, ...
```

The server:
1. Runs `make PROFILE=docs/superpowers/profiles/mcp-test.profile` to build BareMRI
2. Launches the binary with your NIfTI files
3. Reads the ephemeral port from stdout
4. Connects and starts serving MCP tools

## MCP Tools

### Session

| Tool | Description |
|---|---|
| `baremri_list_files` | List all .nii/.nii.gz files under a directory |
| `baremri_open` | Open files in BareMRI. Profiles: laptop/lt (400x830), tall/t (800x970), medium/m (1200x630), large/l (1600x900) |

### Viewer Controls

| Tool | Description |
|---|---|
| `baremri_set_crosshair` | Move crosshair to (x,y,z) voxel coords |
| `baremri_set_focus` | Switch view: axial / sagittal / coronal |
| `baremri_navigate_slice` | Step ±N slices in focused view |
| `baremri_switch_image` | Switch active image by index |
| `baremri_set_zoom` | Set zoom (1.0–32.0) |
| `baremri_set_colormap` | Set colormap: gray / heat / turbo / viridis / inferno |
| `baremri_set_window` | Set contrast window width |
| `baremri_set_level` | Set contrast level |
| `baremri_reset_contrast` | Reset to auto-computed range |
| `baremri_set_timepoint` | Jump to 4D time point |
| `baremri_step_timepoint` | Step ±1 time point |
| `baremri_toggle_overlay` | Show/hide overlay |
| `baremri_set_overlay_threshold` | Set overlay threshold (0.25–20.0) |
| `baremri_set_overlay_opacity` | Set overlay opacity (0.0–1.0) |
| `baremri_set_seg_opacity` | Set segmentation opacity |
| `baremri_screenshot` | Save BMP screenshot |
| `baremri_get_state` | Get full viewer state |

## Testing without MCP

Use `nc` (netcat) to send raw commands once BareMRI is running:

```sh
echo '{"cmd":"get_state"}' | nc localhost <port>
```
