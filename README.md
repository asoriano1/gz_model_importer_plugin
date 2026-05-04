# gz_model_importer_plugin

Gazebo Sim (Harmonic) GUI plugin for ROS 2 Jazzy that provides a modal robot importer workflow with a standard ROS 2 launch entry point.

## Gazebo ROS 2 Model Runtime Suite

This project is part of a broader **Gazebo ROS 2 Model Runtime Suite**: a set of Gazebo GUI tools designed to reduce the gap between loading a model in simulation and running a useful ROS 2 runtime around it.

This package provides the **Gazebo Model Importer** step of that workflow. Its job is to get a robot model into the active Gazebo world quickly, optionally start `robot_state_publisher` for URDF / XACRO models, and hand off sensor bridging to the companion plugin [`gz_ros2_bridge_manager`](https://github.com/asoriano1/gz_ros2_bridge_manager).

Load a local robot description, preview it in the active simulation, configure spawn options, and import it into the Gazebo world in a few clicks.

```text
URDF / XACRO / SDF
       │
       ▼
Gazebo Model Importer
       │
       ├── Preview model
       ├── Spawn final model in Gazebo
       └── Optionally launch robot_state_publisher for URDF/XACRO
       │
       ▼
Gazebo world + ROS 2 TF tree
       │
       ▼
gz_ros2_bridge_manager
       │
       └── Discover and bridge Gazebo sensor topics
       │
       ▼
ROS 2 tools and applications
RViz · Nav2 · MoveIt · custom nodes
```

## Current scope

Implemented in this plugin:

- Model preview before final import
- Final URDF / XACRO / SDF spawn into Gazebo
- Namespace- and prefix-aware import workflow
- Optional `robot_state_publisher` launch for URDF / XACRO models
- Automatic cleanup of `robot_state_publisher` when Gazebo shuts down

Provided by the companion bridge manager:

- Gazebo topic discovery
- Sensor topic classification
- ROS 2 bridge command and process management

Not currently handled by this plugin:

- `ros2_control` controller management
- Nav2 or MoveIt launch
- A full ROS 2 runtime process dashboard
- Automatic `robot_description` support for SDF-only models

## What this enables

With the importer and bridge manager together, a typical user can:

1. Load a URDF, XACRO, or SDF model into a running Gazebo world.
2. Preview and configure the model before spawning it.
3. Spawn the final model with the desired instance name, namespace, and prefix settings.
4. For URDF / XACRO models, automatically start `robot_state_publisher`.
5. Use the bridge manager to expose Gazebo sensors to ROS 2.
6. Inspect the model in RViz or connect higher-level ROS 2 stacks.

## Demo

![Import workflow](demo.gif)

## Features

- **File formats**: URDF, XACRO (expanded via `xacro`), SDF
- **URI resolution**: `model://` and `package://` URIs resolved via `ament_index` and `GZ_SIM_RESOURCE_PATH`
- **Preview**: spawns the robot as a static entity before final import; camera auto-focuses and auto-restores
- **Preview summary**: collapsed card in preview/configuration mode showing detected Gazebo sensors and `ros2_control` controller definitions
- **Auto-selection**: preview entity is automatically selected in the Gazebo Entity Tree on spawn
- **Highlight modes**: Wireframe, Transparent, or None (user-selectable; model appearance unchanged by default)
- **Preflight report**: detects unresolved URIs, Ogre material scripts, mesh collisions, and embedded plugins before import
- **Import options**: Gazebo model name, ROS 2 namespace, ROS 2 frame prefix, and spawn pose (X / Y / Z / Roll / Pitch / Yaw)
- **Sequential defaults**: each newly loaded model gets an incremented default model name; XACRO files exposing `namespace` and `prefix` args also get incremented default ROS 2 namespace and frame prefix values derived from that model name
- **Optional ROS 2 runtime**: launch `robot_state_publisher` automatically after a successful final URDF / XACRO import
- **Resolved robot description reuse**: `robot_description` comes from the same resolved URDF used by the final import pipeline
- **ROS 2 namespace support**: `robot_state_publisher` starts in the selected ROS namespace with `use_sim_time=true`
- **Process cleanup**: `robot_state_publisher` processes started by the plugin are terminated when Gazebo / the plugin shuts down
- **Visual preview badge**: overlay banner on the main Gazebo window during preview mode

## Requirements

| Dependency | Version |
|---|---|
| Ubuntu | 24.04 |
| ROS 2 | Jazzy |
| Gazebo Sim | Harmonic (gz-sim 8) |
| `xacro` | from `ros-jazzy-xacro` |

## Build

```bash
cd <workspace>
source /opt/ros/jazzy/setup.bash
colcon build --symlink-install --packages-select gz_model_importer_plugin
source install/setup.bash
```

## Usage

### Recommended launch flow

Start Gazebo through the package launch file when you want the Robot Importer
panel available immediately and the required Gazebo runtime paths prepared
automatically:

```bash
ros2 launch gz_model_importer_plugin gazebo_importer.launch.py
```

To open a specific world instead:

```bash
ros2 launch gz_model_importer_plugin gazebo_importer.launch.py \
  world:=/absolute/path/to/world.sdf
```

This launch file:

- starts `gz sim`
- loads the Robot Importer GUI plugin through the package GUI config
- prepends the package `lib` directory to `GZ_GUI_PLUGIN_PATH`
- prepends the `gz_ros2_control` `lib` directory to `GZ_SIM_SYSTEM_PLUGIN_PATH` when `gz_ros2_control` is installed
- starts a `ros_gz_bridge` `/clock` bridge when `ros_gz_bridge` is available so ROS nodes using `use_sim_time` can follow Gazebo time

Useful optional arguments:

- `verbose:=4`
- `gui:=false` for server-only mode (`gz sim -s`)
- `paused:=true`
- `bridge_clock:=false` to skip the automatic `/clock` bridge
- `render_engine:=ogre2` or `render_engine:=ogre`

If `world` is omitted, the launch file opens the packaged demo world
`worlds/robotnik_world.sdf`.

### Manual Gazebo start

The package still installs a GUI config and demo worlds for direct `gz sim`
usage when needed. After building and sourcing the workspace, this command
starts the default Robotnik demo world with the plugin already loaded:

```bash
gz sim \
  $(ros2 pkg prefix gz_model_importer_plugin)/share/gz_model_importer_plugin/worlds/robotnik_world.sdf \
  --gui-config $(ros2 pkg prefix gz_model_importer_plugin)/share/gz_model_importer_plugin/config/gz_model_importer_plugin.config
```

If you prefer a lighter alternative world, use:

```bash
gz sim \
  $(ros2 pkg prefix gz_model_importer_plugin)/share/gz_model_importer_plugin/worlds/gazebo_importer_default.sdf \
  --gui-config $(ros2 pkg prefix gz_model_importer_plugin)/share/gz_model_importer_plugin/config/gz_model_importer_plugin.config
```

If `ros2 pkg prefix gz_model_importer_plugin` reports `Package not found`, the
workspace has not been sourced yet or the package has not been built in the
current shell.

### Launch Gazebo with the plugin in your own world

The plugin ships a ready-to-use GUI config:

```bash
gz sim <your_world.sdf> \
  --gui-config $(ros2 pkg prefix gz_model_importer_plugin)/share/gz_model_importer_plugin/config/gz_model_importer_plugin.config
```

The `ros2 launch gz_model_importer_plugin gazebo_importer.launch.py` entry point is
still the recommended flow because it also prepares `GZ_GUI_PLUGIN_PATH` and
`GZ_SIM_SYSTEM_PLUGIN_PATH` for you.

Or add the plugin entry to an existing world file's `<gui>` section:

```xml
<plugin filename="RobotImporterGui" name="Robot Importer"/>
```

### Workflow

1. Click **Browse** and select a `.urdf`, `.xacro`, or `.sdf` file.
2. Review the **preflight report** (unresolved URIs, mesh collisions, detected plugins).
3. Adjust the **Model name** that will be used for the Gazebo entity.
4. For XACRO files exposing `namespace` / `prefix` args, adjust the default **Namespace** and **Frame prefix** values used for ROS 2 topics and frames.
5. For URDF / XACRO inputs, optionally enable **Launch robot_state_publisher after import**.
6. Expand **Pose** to set the spawn position and orientation.
7. Click **Import** — the plugin spawns the robot into the world.

A preview entity (static, plugins stripped) is spawned automatically before the final import so you can inspect the model in the scene first. Click **Cancel** to discard the preview without importing. `robot_state_publisher` is never launched during preview.

During preview/configuration, the plugin also shows a compact collapsed summary card with the detected Gazebo sensor count and any controller definitions found in referenced `ros2_control` parameter files.

## URI resolution order

For `model://PACKAGE/...` and `package://PACKAGE/...` URIs:

1. Same directory as the source file (self-reference)
2. Sibling directory with matching name
3. Directories listed in `GZ_SIM_RESOURCE_PATH`
4. `ament_index` package share directory

Make sure your workspace is sourced (`source install/setup.bash`) so that `ament_index` can find your robot description packages.

## Multi-instance notes

The **Model name** field sets the Gazebo entity name and the top-level SDF model name. For each newly loaded file, the plugin proposes a unique sequential default such as `sensor_test_robot_1`, `sensor_test_robot_2`, and so on.

When a XACRO file declares `namespace` and / or `prefix` arguments, the UI exposes matching **Namespace** and **Frame prefix** fields. Their defaults are derived from the proposed model name, so a model named `sensor_test_robot_3` starts with:

- Namespace: `sensor_test_robot_3`
- Frame prefix: `sensor_test_robot_3_`

> The importer rewrites the top-level model name and passes ROS-related overrides only where it recognizes them. `namespace` is injected into supported ROS plugin elements, and `prefix` is forwarded as a XACRO argument when the description declares it. The plugin does **not** rewrite arbitrary hardcoded topic names, service names, or TF frame IDs inside every possible plugin configuration, so full isolation still depends on how the robot description itself is authored.

## Test models

The `test/` directory contains ready-to-use models for manual validation:

| File | Purpose |
|---|---|
| `test/models/simple_box.urdf` | Minimal URDF, baseline check |
| `test/models/xacro_with_defaults.urdf.xacro` | XACRO with default arguments |
| `test/models/xacro_mixed_args.urdf.xacro` | XACRO requiring explicit arguments |
| `test/models/minimal_sdf.sdf` | Minimal SDF entrypoint |
| `test/models/sensor_test_robot.urdf.xacro` | Multi-sensor robot (IMU, LiDAR, camera) |
| `test/models/mesh_uri_test.urdf.xacro` | URI resolution edge cases |
| `test/worlds/importer_test.sdf` | Walled test world for sensor validation |
| `test/worlds/robotnik_world.sdf` | Robotnik environment world |

See `test/models/README.md` for details on each model and the expected behaviour.

## ROS 2 runtime support

For URDF and XACRO inputs, the importer can optionally start `robot_state_publisher` after a successful final import. Enable **Launch robot_state_publisher after import** in the import options before pressing **Import**.

This option is available only for URDF / XACRO models. For SDF-only models it is disabled, because the importer does not have a URDF `robot_description` to publish.

After a successful final import, the importer:

- reuses the resolved URDF from the final import pipeline as `robot_description`
- starts `robot_state_publisher` in the selected ROS namespace
- sets `use_sim_time=true`
- keeps preview behavior unchanged: nothing is launched during preview
- terminates the started `robot_state_publisher` process when Gazebo / the plugin shuts down

You can verify the runtime state from a sourced ROS 2 shell:

```bash
ros2 node list
ros2 param get /<namespace>/robot_state_publisher use_sim_time
ros2 topic echo /tf_static
pgrep -af robot_state_publisher
```

To bridge Gazebo sensor topics to ROS 2 after import, use the separate **ROS 2 Bridge Manager** plugin, [`gz_ros2_bridge_manager`](https://github.com/asoriano1/gz_ros2_bridge_manager), which inspects models already present in the Gazebo world and manages `ros_gz_bridge` instances.

Models with `ros2_control`-related elements still require an external controller launch setup. The package launch now bridges `/clock` automatically when `ros_gz_bridge` is available, which helps `controller_manager` instances running with `use_sim_time=true`.

## Known limitations

- `robot_state_publisher` launch is not available for SDF-only models.
- There is no UI yet to stop or restart `robot_state_publisher` manually once launched.
- `robot_state_publisher` is launched only after the final import succeeds, never during preview.
- A ROS namespace does not automatically prefix TF frame IDs.
- Multi-robot TF consistency still depends on how the URDF / XACRO uses namespace and prefix arguments.
- XACRO files with required arguments that cannot be inferred are not supported without manual parameter injection.
- Mesh collision shapes (e.g. convex decomposition) may not load in the preview physics engine; this does not affect the visual preview.
- Switching highlight mode from Transparent back to None restores opacity but may not fully restore all material properties if the original material used engine-specific extensions.

## Author

Ángel Soriano — [Robotnik Automation S.L.L.](https://robotnik.es)
