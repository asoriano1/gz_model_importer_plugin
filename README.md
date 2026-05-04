# gz_model_importer_plugin

Gazebo Harmonic GUI plugin for ROS 2 Jazzy that imports URDF, XACRO, and SDF models into a running Gazebo world.

## Gazebo ROS 2 Model Runtime Suite

This package is part of the **Gazebo ROS 2 Model Runtime Suite**:

1. **Model Importer** (`gz_model_importer_plugin`)
   Imports a model into Gazebo, supports preview, final spawn, and optional `robot_state_publisher` for URDF / XACRO.
2. **Bridge Manager** (`gz_ros2_bridge_manager`)
   Discovers active Gazebo sensor topics and launches the required ROS 2 bridges.
3. **Control Manager** (`gz_ros2_control_manager`, in development)
   Discovers `controller_manager` instances, hardware interfaces, and controllers, and provides a UI to load, configure, and activate existing controllers.

This repository provides the **Model Importer** step.

```mermaid
flowchart LR
  A[URDF / XACRO / SDF] --> B[Model Importer<br/>preview + spawn + optional robot_state_publisher]
  B --> C[Gazebo world]
  C --> D[Bridge Manager<br/>discover sensors/topics + run ros_gz_bridge]
  C --> E[Control Manager<br/>in development]
  D --> F[ROS 2 applications<br/>RViz / Nav2 / MoveIt / custom nodes]
  E --> F
```

## What It Does

- Imports local URDF, XACRO, and SDF models into the active Gazebo world
- Creates a preview before the final spawn
- Lets the user set model name, namespace, frame prefix, and pose
- Optionally starts `robot_state_publisher` after importing URDF / XACRO models
- Provides a standard ROS 2 launch entry point that opens Gazebo with the plugin already loaded

## Demo

![Import workflow](demo.gif)

## Requirements

- Ubuntu 24.04
- ROS 2 Jazzy
- Gazebo Harmonic (`gz-sim8`, `gz-gui8`)
- `xacro` for XACRO inputs

## Build

```bash
cd <workspace>
source /opt/ros/jazzy/setup.bash
colcon build --symlink-install --packages-select gz_model_importer_plugin
source install/setup.bash
```

## Recommended Start

Start Gazebo through the package launch file:

```bash
ros2 launch gz_model_importer_plugin gazebo_importer.launch.py
```

To open a specific world:

```bash
ros2 launch gz_model_importer_plugin gazebo_importer.launch.py \
  world:=/absolute/path/to/world.sdf
```

If `world` is omitted, the packaged demo world `robotnik_world.sdf` is used.

This launch flow:

- opens Gazebo with the Robot Importer panel already loaded
- prepares `GZ_GUI_PLUGIN_PATH`
- prepares `GZ_SIM_SYSTEM_PLUGIN_PATH` for `gz_ros2_control` when it is installed
- starts a `/clock` bridge if `ros_gz_bridge` is available

Useful arguments:

- `verbose:=4`
- `paused:=true`
- `gui:=false`
- `bridge_clock:=false`
- `render_engine:=ogre2`

## Manual Gazebo Start

If you want to start Gazebo directly, the package installs a ready-to-use GUI config:

```bash
gz sim \
  $(ros2 pkg prefix gz_model_importer_plugin)/share/gz_model_importer_plugin/worlds/robotnik_world.sdf \
  --gui-config $(ros2 pkg prefix gz_model_importer_plugin)/share/gz_model_importer_plugin/config/gz_model_importer_plugin.config
```

## Import Workflow

1. Start Gazebo with the launch file above.
2. Click **Browse** and select a `.urdf`, `.xacro`, or `.sdf` file.
3. Review the preview and adjust **Model name**, **Namespace**, **Frame prefix**, and **Pose** if needed.
4. For URDF / XACRO models, leave **Launch robot_state_publisher after import** enabled if you want the TF tree published in ROS 2.
5. Click **Import** to spawn the model into the current world.
6. Use `gz_ros2_bridge_manager` to expose the model's Gazebo sensor topics to ROS 2.

## Notes

- `robot_state_publisher` is available only for URDF / XACRO models.
- Topic bridging is handled by the companion package `gz_ros2_bridge_manager`.

## Author

Ángel Soriano — [Robotnik Automation S.L.L.](https://robotnik.es)
