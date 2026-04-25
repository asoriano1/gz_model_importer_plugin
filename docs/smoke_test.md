# Robot Importer GUI — Manual Smoke Test

**Target:** ROS 2 Jazzy + Gazebo Harmonic  
**Scope:** MVP functional verification. Not a unit test; requires a running Gazebo instance.

---

## Environment setup

```bash
# Terminal A — Gazebo world
source /opt/ros/jazzy/setup.bash
source install/setup.bash
gz sim -v 4 shapes.sdf \
  --gui-config src/robot_importer_gui/config/robot_importer_gui.config

# Terminal B — monitor importer logs
gz log --verbose 4   # or watch Terminal A's stderr for [robot_importer_gui] lines
```

Confirm the Robot Importer panel appears in the Gazebo GUI sidebar.

---

## Test assets

### Asset A — minimal URDF (no plugins, no sensors)

`/tmp/test_robot.urdf`:

```xml
<?xml version="1.0"?>
<robot name="test_robot">
  <link name="base_link">
    <visual>
      <geometry><box size="0.5 0.3 0.2"/></geometry>
    </visual>
    <collision>
      <geometry><box size="0.5 0.3 0.2"/></geometry>
    </collision>
    <inertial>
      <mass value="1.0"/>
      <inertia ixx="0.01" iyy="0.01" izz="0.01" ixy="0" ixz="0" iyz="0"/>
    </inertial>
  </link>
  <link name="arm_link">
    <visual>
      <geometry><cylinder radius="0.05" length="0.4"/></geometry>
    </visual>
    <inertial>
      <mass value="0.5"/>
      <inertia ixx="0.001" iyy="0.001" izz="0.001" ixy="0" ixz="0" iyz="0"/>
    </inertial>
  </link>
  <joint name="base_to_arm" type="revolute">
    <parent link="base_link"/>
    <child link="arm_link"/>
    <origin xyz="0 0 0.2" rpy="0 0 0"/>
    <axis xyz="0 0 1"/>
    <limit lower="-1.57" upper="1.57" effort="10" velocity="1"/>
  </joint>
</robot>
```

### Asset B — minimal XACRO (single file, no arguments)

`/tmp/test_robot.xacro`:

```xml
<?xml version="1.0"?>
<robot name="xacro_robot" xmlns:xacro="http://ros.org/wiki/xacro">
  <xacro:property name="body_size" value="0.4"/>
  <link name="base_link">
    <visual>
      <geometry><box size="${body_size} 0.2 0.1"/></geometry>
    </visual>
    <inertial>
      <mass value="2.0"/>
      <inertia ixx="0.02" iyy="0.02" izz="0.02" ixy="0" ixz="0" iyz="0"/>
    </inertial>
  </link>
</robot>
```

### Asset C — URDF with ROS plugin (for plugin-stripping verification)

`/tmp/plugin_robot.urdf`:

```xml
<?xml version="1.0"?>
<robot name="plugin_robot">
  <link name="base_link">
    <visual><geometry><sphere radius="0.2"/></geometry></visual>
    <inertial>
      <mass value="1.0"/>
      <inertia ixx="0.01" iyy="0.01" izz="0.01" ixy="0" ixz="0" iyz="0"/>
    </inertial>
  </link>
  <gazebo>
    <plugin filename="libgazebo_ros_joint_state_publisher.so"
            name="joint_state_publisher">
      <ros><namespace>/plugin_robot</namespace></ros>
      <update_rate>50</update_rate>
    </plugin>
  </gazebo>
</robot>
```

### Asset D — URDF with missing mesh (intentional failure)

`/tmp/broken_robot.urdf`:

```xml
<?xml version="1.0"?>
<robot name="broken_robot">
  <link name="base_link">
    <visual>
      <geometry>
        <mesh filename="package://nonexistent_pkg/meshes/base.dae"/>
      </geometry>
    </visual>
    <inertial>
      <mass value="1.0"/>
      <inertia ixx="0.01" iyy="0.01" izz="0.01" ixy="0" ixz="0" iyz="0"/>
    </inertial>
  </link>
</robot>
```

---

## Test cases

---

### TC-01: Simple URDF import (no preview)

**Asset:** A (`test_robot.urdf`)  
**Instance name:** `robot_a`

**Steps:**
1. In the Robot Importer panel, click "Select File" → choose `/tmp/test_robot.urdf`.
2. Verify the panel shows state "Ready" and detected format "URDF".
3. Set Instance Name to `robot_a`. Leave ROS namespace and frame prefix empty.
4. Click "Import".
5. Observe state transition: Ready → Spawning → Done.

**Expected:**
- State reaches "Done".
- In Gazebo entity tree: model `robot_a` appears.
- Two links visible: `base_link`, `arm_link`.
- No ROS topics published (verify: `ros2 topic list` — no new topics).
- Log: `[robot_importer_gui] Spawn complete: robot_a`

**Pass / Fail:**

---

### TC-02: Simple XACRO import (no preview)

**Asset:** B (`test_robot.xacro`)  
**Instance name:** `xacro_bot`

**Steps:**
1. Click "Select File" → choose `/tmp/test_robot.xacro`.
2. Verify state transitions: Expanding → Ready.
3. Set Instance Name to `xacro_bot`.
4. Click "Import".

**Expected:**
- State reaches "Done".
- Model `xacro_bot` appears in scene with `base_link` of size 0.4 × 0.2 × 0.1.
- Log: `[robot_importer_gui] SDF ready` (confirms XACRO → URDF → SDF pipeline completed).

**Pass / Fail:**

---

### TC-03: Two imports of the same robot with different instance names

**Asset:** A (`test_robot.urdf`)  
**Instance names:** `robot_1` and `robot_2`

**Steps:**
1. Import `test_robot.urdf` as `robot_1` (TC-01 flow). Confirm Done.
2. Click "Reset".
3. Re-select `/tmp/test_robot.urdf`.
4. Set Instance Name to `robot_2`.
5. Click "Import".

**Expected:**
- Both `robot_1` and `robot_2` appear in the entity tree as separate models.
- No name conflict error.
- State reaches "Done" for the second import.

**Failure mode to watch:** If instance name was not changed before TC-03 step 5, the server should refuse the spawn (duplicate name). Verify the error message is surfaced in the UI: "Server refused spawn request for entity 'robot_1'".

**Pass / Fail:**

---

### TC-04: Preview → Cancel

**Asset:** A (`test_robot.urdf`)  
**Instance name:** `preview_test`

**Steps:**
1. Load `test_robot.urdf`. Wait for Ready.
2. Note whether the world is currently paused (toolbar indicator).
3. Click "Preview".
4. Observe state: Previewing → Configuring.
5. Verify entity `__preview_preview_test` appears in the entity tree.
6. Verify world is paused (toolbar).
7. Click "Cancel Preview".
8. Observe state: Configuring → (removal in-flight) → Ready.

**Expected:**
- `__preview_preview_test` is removed from the entity tree.
- World returns to its pre-preview pause state (if it was running, it resumes; if it was paused, it stays paused).
- State returns to "Ready".
- No final `preview_test` entity exists in the scene.
- Log: `[robot_importer_gui] Preview cancelled — returning to Ready.`

**Verify plugin stripping:** In Gazebo entity tree, confirm NO `joint_state_publisher` plugin was loaded during preview for Asset C. Repeat with `plugin_robot.urdf`:
- Preview should show the sphere geometry.
- `ros2 topic list` must NOT show `/plugin_robot/joint_states` during preview.

**Pass / Fail:**

---

### TC-05: Preview → Import

**Asset:** A (`test_robot.urdf`)  
**Instance name:** `confirmed_bot`

**Steps:**
1. Load `test_robot.urdf`. Wait for Ready.
2. Click "Preview".
3. Wait for Configuring state. Verify `__preview_confirmed_bot` in entity tree.
4. Inspect the model geometry visually (2 m above origin).
5. Optionally edit instance name (still `confirmed_bot`).
6. Click "Import".
7. Observe state: Configuring → Spawning → Done.

**Expected:**
- `__preview_confirmed_bot` is removed from the entity tree.
- `confirmed_bot` (the final entity) appears at world origin (or SDF-defined pose — NOT at 2 m offset).
- World pause state is restored before final spawn begins.
- State reaches "Done".
- Log shows removal, then spawn of `confirmed_bot`.

**Critical check — preview vs final pose:** The preview appears at z = 2.0 m. The final entity MUST appear at z = 0 (or whatever the SDF defines). If both land at z = 2.0 m, `EntityFactory::pose` is being applied to the final spawn — this is a bug.

**Pass / Fail:**

---

### TC-06: Intentional failure — missing mesh resource

**Asset:** D (`broken_robot.urdf`)

**Steps:**
1. Load `broken_robot.urdf`.
2. Observe state transition.

**Expected outcomes (two possible):**

**Outcome A — sdformat rejects the file at load time:**
- State goes to ConversionFailed.
- `lastError` shows something like: `unresolvable URI: package://nonexistent_pkg/meshes/base.dae` or similar sdformat error.
- The file never reaches Spawning.

**Outcome B — sdformat accepts the file (mesh is optional at parse time):**
- State reaches Ready.
- The user can attempt to preview or import.
- The model spawns without the mesh (invisible geometry or warning in Gazebo console).
- The importer reaches Done but Gazebo logs a mesh-not-found warning.

Both are acceptable behavior. Document which outcome you observe.

**Note:** sdformat14's handling of unresolvable `package://` URIs depends on whether `ament_index` lookup is active in the process. It may warn without failing. This is a known limitation: the importer validates SDF structure, not resource availability.

**Pass / Fail and observed outcome (A or B):**

---

### TC-07: Spawn failure — duplicate instance name

**Asset:** A (`test_robot.urdf`)  
**Instance name:** `duplicate`

**Steps:**
1. Import `test_robot.urdf` as `duplicate`. Confirm Done.
2. Click "Reset".
3. Re-select `/tmp/test_robot.urdf`.
4. Set Instance Name to `duplicate` (same as before).
5. Click "Import".

**Expected:**
- Spawn service returns failure (Gazebo refuses duplicate model names by default unless `allow_renaming` is set).
- State transitions to "SpawnFailed".
- `lastError` shows "Server refused spawn request for entity 'duplicate'".
- The user can change the instance name and click "Import" again to retry without resetting.
  - Change instance name to `duplicate_2` → click Import → should succeed.

**Pass / Fail:**

---

## Verification checklist

| Check | Method |
|-------|--------|
| World pause restored after cancel | Compare toolbar pause indicator before/after |
| World pause restored after confirm+import | Same |
| No ROS topics from preview | `ros2 topic list` during preview state |
| Preview entity removed before final spawn | Entity tree shows only final name |
| Final spawn at SDF pose (not preview offset) | gz::sim inspector or `gz model -m <name> -p` |
| gzmsg logs present | Gazebo terminal output, `-v 4` verbosity |
| SpawnFailed allows retry | TC-07 step: change name, click Import |
| Plugin-stripped preview | TC-04 variant with `plugin_robot.urdf` |

---

## Known residual risks

| Risk | Severity | Notes |
|------|----------|-------|
| **In-flight spawn orphan on reset** | Low | If `reset()` is called while preview spawn service call is in-flight (typically 100–5000 ms window), the preview entity appears and is never removed. The user must delete it manually from the entity tree. Frequency: only if user resets extremely quickly after clicking Preview. |
| **Sensor GPU structures during preview** | Low | `<sensor>` elements are not stripped. Sensors exist in Gazebo's ECS and may consume GPU resources. With world paused, they do not fire. If user steps the world during preview, sensors run but publish nothing (bridge plugin stripped). |
| **World pause race on fast preview fail** | Very low | `setWorldPaused(true)` and the spawn request are both async. If spawn fails before the pause completes, `restoreWorldPause()` may race with the original pause. Net effect: world may end up paused when it should be running. User can resume manually. |
| **SDF mesh URI not validated** | Medium | Missing `package://` meshes may not fail at SDF load time (sdformat may defer URI resolution). The model spawns without the mesh geometry, silently. There is currently no pre-spawn URI resolution step. |
| **World name stale after world reload** | Low | `worldName_` is cached after first discovery. If the world is closed and reopened with a different name, the cache is stale. A plugin restart (via Reset + re-open) clears it. |
| **`ros_namespace` and `frame_prefix` not applied** | By design | Both options are accepted in the UI but not injected into the SDF. A warning is shown. User must configure namespace manually in each plugin's SDF block. |
| **No pose configuration in preview** | By design | The preview always spawns at (0, 0, 2.0 m). The final spawn uses whatever `<pose>` is in the SDF. There is no way to set the spawn pose through the UI in this MVP. |
| **XACRO arguments not surfaced in UI** | By design | XACRO files that require `--arg key:=value` arguments cannot be expanded without them. The UI currently has no field for XACRO arguments; they must be passed via `setXacroArgs()` from code before the file is selected. |
