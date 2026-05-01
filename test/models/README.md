# Test models for gz_model_importer_gui

Minimal robot descriptions used to exercise specific importer code paths.

## Models

| File | Format | What it tests |
|---|---|---|
| `simple_box.urdf` | URDF | Basic URDF→SDF conversion, no xacro, no URIs |
| `xacro_with_defaults.urdf.xacro` | XACRO | All `<xacro:arg>` have defaults → expands without user input |
| `xacro_mixed_args.urdf.xacro` | XACRO | One arg without default (`robot_id`) → importer passes `robot_id:=` (empty) |
| `minimal_sdf.sdf` | SDF 1.9 | Direct SDF path; one plugin detected in preflight and stripped in preview |
| `sensor_test_robot.urdf.xacro` | XACRO | IMU + LiDAR + Camera sensors; 3 plugins stripped in preview, active after import |
| `mesh_uri_test.urdf.xacro` | XACRO | `package://robotnik_description/meshes/...` URI rewriting via ament_index |

---

## Detailed test guide

### simple_box.urdf
**Expected preflight:** `URIs: 0/0 resolved` — no URIs, no plugins.
**Expected preview:** solid box + cylinder, no errors.

---

### xacro_with_defaults.urdf.xacro
**Expected console:**
```
[gz_model_importer_gui] XACRO args discovered: 4, effective arg list:
  [robot_name:=test_robot] [prefix:=] [body_color:=0.2 0.6 0.2 1.0] [body_mass:=10.0]
```
**Expected preflight:** no plugins, no unresolved URIs.
**Expected preview:** differential-drive robot with 2 wheels and a mast.

---

### xacro_mixed_args.urdf.xacro
**Expected console:**
```
[gz_model_importer_gui] XACRO args discovered: 3, effective arg list:
  [prefix:=] [use_arm:=false] [robot_id:=]
```
**Expected preview:** flat chassis only (`use_arm:=false` disables the arm block).

---

### minimal_sdf.sdf
**Expected preflight:**
```
Plugins stripped for preview (1):
  • gz-sim-diff-drive-system
```
**Expected preview:** chassis + lidar cylinder, no diff-drive.
**Expected import:** model spawns with the diff-drive plugin active
(requires diff-drive joints to exist in the world — joints are missing here
by design; use for plugin-stripping tests only).

---

### sensor_test_robot.urdf.xacro
**Expected preflight:**
```
Plugins stripped for preview (3):
  • gz-sim-imu-system
  • gz-sim-sensors-system
  • gz-sim-ros-node-system
```
**Expected preview:** chassis + small IMU box + lidar cylinder + red camera block.
No sensor topics should be published during preview (plugins stripped).

**After final import — start the bridge:**
```bash
# bridge_node accepts a config file; parameter_bridge only takes CLI args
# sensor_test_bridge.yaml matches the XACRO default namespace "sensor_test"
# If you keep the importer-proposed namespace, duplicate the file and
# replace /sensor_test with the namespace shown in the UI.
ros2 run ros_gz_bridge bridge_node \
  --ros-args -p config_file:=$(pwd)/sensor_test_bridge.yaml
```

**Verify sensor topics:**
```bash
ros2 topic list | grep sensor_test
# Expected:
#   /sensor_test/camera/camera_info
#   /sensor_test/camera/image_raw
#   /sensor_test/imu/data_raw
#   /sensor_test/scan

ros2 topic hz /sensor_test/imu/data_raw    # ~100 Hz
ros2 topic hz /sensor_test/scan            # ~10 Hz
ros2 topic hz /sensor_test/camera/image_raw  # ~30 Hz
```

**Requisito del mundo:** `gz-sim-sensors-system` y `gz-sim-imu-system` deben
ser **world plugins** — como model plugins lidar y camera no funcionan porque
necesitan el pipeline de renderizado del mundo. El IMU sí funciona como model
plugin (no necesita rendering).

Opción 1 — añadir al world SDF:
```xml
<plugin filename="gz-sim-sensors-system" name="gz::sim::systems::Sensors">
  <render_engine>ogre2</render_engine>
</plugin>
<plugin filename="gz-sim-imu-system" name="gz::sim::systems::Imu"/>
```

Opción 2 — usar el mundo de ros_gz_sim que ya los incluye:
```bash
ros2 launch ros_gz_sim gz_sim.launch.py gz_args:="-r empty.sdf"
```

---

### mesh_uri_test.urdf.xacro
Requires `robotnik_description` to be built and sourced.

**Expected preflight:**
```
URIs: 4/4 resolved
```
(2 visual + 2 collision mesh URIs — all rewritten from `package://` to `file://`)

**Expected preview:** rbsummit chassis mesh + rubber wheel mesh rendered in Gazebo.

If `robotnik_description` is not in the ament index:
```
URIs: 0/4 resolved — 4 unresolved:
  • package://robotnik_description/meshes/bases/rbsummit/rbsummit_xl_chassis_simple.stl
  • ...
```

**Manual check:**
```bash
# Confirm the package is found:
ros2 pkg prefix robotnik_description
# Should print: <workspace>/install/robotnik_description
```

---

## Coverage map

| Code path | Covered by |
|---|---|
| URDF→SDF conversion | simple_box, mesh_uri_test |
| XACRO expansion, all-defaults | xacro_with_defaults |
| XACRO expansion, empty fallback | xacro_mixed_args |
| Direct SDF load | minimal_sdf |
| `package://` URI rewriting | mesh_uri_test |
| Mesh file existence check | mesh_uri_test |
| Plugin preflight detection | minimal_sdf, sensor_test_robot |
| Plugin stripping in preview | minimal_sdf, sensor_test_robot |
| Sensor definitions (IMU/LiDAR/Camera) | sensor_test_robot |
| Sensor topic bridging | sensor_test_robot + sensor_test_bridge.yaml |
| Ogre2 material fix | mesh_uri_test (via robotnik_description STL meshes) |

## What is NOT covered yet

- **Full robotnik robot** (rbvogui, rb1): real multi-file XACRO include tree,
  many meshes, ros2_control. Test manually by opening a robot XACRO directly.
- **Multi-instance spawn**: spawn the same model twice with different names/namespaces.
  Test manually: import twice, change name/namespace field between imports.
- **TF frame verification**: `ros2 topic echo /tf` after import to confirm
  frame names match the instance prefix.
