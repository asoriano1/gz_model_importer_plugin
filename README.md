# robot_importer_gui

Gazebo Sim (Harmonic) GUI plugin for ROS 2 Jazzy that provides a modal robot importer workflow — no launch files required.

Load a local robot description, preview it in the active simulation, configure spawn options, and import it into the Gazebo world in a few clicks.

## Demo

![Import workflow](demo.gif)

![Preview and spawn](demo2.gif)

## Features

- **File formats**: URDF, XACRO (expanded via `xacro`), SDF
- **URI resolution**: `model://` and `package://` URIs resolved via `ament_index` and `GZ_SIM_RESOURCE_PATH`
- **Preview**: spawns the robot as a static entity before final import; camera auto-focuses and auto-restores
- **Auto-selection**: preview entity is automatically selected in the Gazebo Entity Tree on spawn
- **Highlight modes**: Wireframe, Transparent, or None (user-selectable; model appearance unchanged by default)
- **Preflight report**: detects unresolved URIs, Ogre material scripts, mesh collisions, and embedded plugins before import
- **Import options**: instance name, ROS namespace, spawn pose (X / Y / Z / Roll / Pitch / Yaw)
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
colcon build --packages-select robot_importer_gui
source install/setup.bash
```

## Usage

### Launch Gazebo with the plugin

The plugin ships a ready-to-use GUI config:

```bash
gz sim --gui-config $(ros2 pkg prefix robot_importer_gui)/share/robot_importer_gui/config/robot_importer_gui.config
```

Or add the plugin entry to an existing world file's `<gui>` section:

```xml
<plugin filename="RobotImporterGui" name="Robot Importer"/>
```

### Workflow

1. Click **Browse** and select a `.urdf`, `.xacro`, or `.sdf` file.
2. Review the **preflight report** (unresolved URIs, mesh collisions, detected plugins).
3. Expand **Pose** to set the spawn position and orientation.
4. Set **Name** and **Namespace** for multi-instance deployments.
5. Click **Import** — the plugin spawns the robot into the world.

A preview entity (static, plugins stripped) is spawned automatically before the final import so you can inspect the model in the scene first. Click **Cancel** to discard the preview without importing.

## URI resolution order

For `model://PACKAGE/...` and `package://PACKAGE/...` URIs:

1. Same directory as the source file (self-reference)
2. Sibling directory with matching name
3. Directories listed in `GZ_SIM_RESOURCE_PATH`
4. `ament_index` package share directory

Make sure your workspace is sourced (`source install/setup.bash`) so that `ament_index` can find your robot description packages.

## Multi-instance notes

The **Name** field sets the Gazebo entity name and the SDF model name. The **Namespace** field is passed as the ROS namespace to all plugins in the description.

> The importer rewrites the top-level model name and the ROS namespace parameter. It does **not** automatically rewrite hardcoded topic names, service names, or TF frame IDs inside individual plugin configurations. For full isolation between multiple instances of the same robot, ensure your robot description uses namespaced topics and parameterised frame prefixes.

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

## ROS 2 bridge / runtime note

The importer does **not** launch ROS 2 bridges or manage runtime processes.

When a loaded model contains native Gazebo sensors (camera, lidar, IMU, …) or ROS-related plugins, the importer shows a compact informational hint after import. No bridge commands are generated and no external processes are started.

To bridge Gazebo sensor topics to ROS 2 after import, use the separate **ROS 2 Bridge Manager** plugin (`gz_ros2_bridge_manager`), which inspects models already present in the Gazebo world and manages `ros_gz_bridge` instances.

Models with `ros2_control`-related elements require an external controller launch setup — this is also outside the scope of the importer.

## Known limitations

- XACRO files with required arguments that cannot be inferred are not supported without manual parameter injection.
- Mesh collision shapes (e.g. convex decomposition) may not load in the preview physics engine; this does not affect the visual preview.
- Switching highlight mode from Transparent back to None restores opacity but may not fully restore all material properties if the original material used engine-specific extensions.

## Author

Ángel Soriano — [Robotnik Automation S.L.L.](https://robotnik.es)
