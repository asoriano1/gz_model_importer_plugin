# Test models for robot_importer_gui

Minimal robot descriptions used to exercise specific importer code paths.
No external packages or mesh files are required — all geometry is primitive.

## Models

| File | Format | Purpose |
|---|---|---|
| `simple_box.urdf` | URDF | Basic URDF→SDF conversion, no xacro |
| `xacro_with_defaults.urdf.xacro` | XACRO | All `<xacro:arg>` have defaults → must expand without user input |
| `xacro_mixed_args.urdf.xacro` | XACRO | Mix: some args with defaults, `robot_id` without → importer passes `robot_id:=` (empty) |
| `minimal_sdf.sdf` | SDF 1.9 | Direct SDF path; includes a plugin to test preflight stripping |

## What each model tests

### simple_box.urdf
- URDF parse and URDF→SDF conversion via `UrdfToSdf`
- No URI rewriting needed
- Preflight: 0 URIs, 0 Ogre materials, 0 plugins

### xacro_with_defaults.urdf.xacro
- `XacroExpander::discoverArgs()` finds 4 args, all with defaults
- `ImporterBackend::startFileLoad` builds effective arg list from file defaults
- `xacro` is called with `robot_name:=test_robot prefix:= body_color:=0.2 0.6 0.2 1.0 body_mass:=10.0`
- Model expands and previews without user interaction

### xacro_mixed_args.urdf.xacro
- `discoverArgs()` finds 3 args: `prefix` and `use_arm` have defaults, `robot_id` does not
- Importer passes `robot_id:=` (empty string) — xacro accepts an empty value
- Tests the no-default fallback path
- `use_arm:=false` → arm link is excluded from the model (xacro:if block)

### minimal_sdf.sdf
- Direct SDF load path (no URDF conversion)
- `SdfPreflightChecker` detects the `gz-sim-diff-drive-system` plugin
- `PreviewController::preparePreviewSdf()` strips it for preview
- Sensor type is `lidar` (Gazebo Harmonic format, not the legacy `ray`)

## How to use

Open Gazebo Harmonic, load the importer plugin, and browse to each file.
Check the preflight report panel and the Gazebo console for expected messages.
