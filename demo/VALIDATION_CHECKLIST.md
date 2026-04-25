# Robot Importer GUI — Pre-Complex-Model Validation Checklist

Use `demo/importer_test_box.sdf` for all checks unless noted.
Launch: `gz sim empty.sdf` then open the Robot Importer plugin.

---

## 1. Basic import flow

- [ ] Browse → select `importer_test_box.sdf`
- [ ] Auto-preview spawns within 2 s (status: Configuring)
- [ ] Instance name proposed automatically (e.g. `importer_test_box_1`)
- [ ] Log shows: `Preview entity alive: __preview_importer_test_box_1`
- [ ] Click **Import** → status becomes Done
- [ ] Log shows: `Spawn complete: importer_test_box_1`
- [ ] Final entity visible in world at the same pose as the preview
- [ ] Click **Reset** → status returns to Idle, panel clears

---

## 2. Preview mode visual indicators

- [ ] Orange **PREVIEW MODE — Simulation paused** banner appears at top of panel
- [ ] Gazebo main window shows `PREVIEW MODE — Simulation paused` snackbar (bottom of viewport)
- [ ] Preview entity renders semi-transparent (alpha ~0.4 vs final entity opacity)
- [ ] On cancel: snackbar changes to `Preview cancelled — resume simulation manually`
- [ ] On import: snackbar changes to `Robot imported — resume simulation manually`
- [ ] Both banners disappear / change correctly (do not persist after session ends)

---

## 3. Pose synchronisation

### 3a. Panel → Gazebo
- [ ] Change X to 5 in the panel, wait 600 ms
- [ ] Log shows: `Moving preview to pos=(5,0,0) rpy=(0,0,0)`
- [ ] Log shows: `set_pose succeeded for '__preview_...'`
- [ ] Preview entity visually moves to x=5
- [ ] Repeat with Y=3, Z=1; confirm each axis independently
- [ ] Change roll/pitch/yaw; confirm entity rotates in viewport

### 3b. Gazebo → Panel (TransformControl sync)
- [ ] Select preview entity with TransformControl gizmo, drag it
- [ ] Panel pose fields update to match Gazebo position in real time
- [ ] No "Moving preview to (0,0,0)" regression (debounce guard active)

### 3c. Final spawn pose
- [ ] Position preview at (4, 2, 0) using the panel
- [ ] Click Import
- [ ] Final entity spawns at exactly (4, 2, 0) — not at origin

---

## 4. Robustness / edge cases

- [ ] **World running during import**: start simulation (Play), then browse+import
  - World must be auto-paused before preview spawn (log: `pauseWorldSync`)
  - No DART/ODE physics crash in Gazebo terminal
- [ ] **Reset mid-preview**: click Reset while preview is live
  - Preview entity removed from scene
  - Status returns to Idle
  - No orphan entity left in world
- [ ] **Re-import same file**: browse same SDF a second time
  - New instance name proposed (e.g. `importer_test_box_2`)
  - No name collision in world
- [ ] **Import without preview** (SpawnFailed → Edit name → Import):
  - Not a primary flow; note whether it works or not
- [ ] **Gazebo not running**: open plugin before `gz sim`
  - Appropriate error shown in panel, no crash

---

## 5. Log cleanliness (no importer-introduced noise)

Expected clean log with `importer_test_box.sdf` (no warnings from importer):
```
[robot_importer_gui] World discovered: default
[robot_importer_gui] SDF loaded (N chars, URIs rewritten). Step 2: auto-preview.
[robot_importer_gui] Spawning preview '__preview_importer_test_box_1' ...
[robot_importer_gui] Preview entity alive: __preview_importer_test_box_1
[robot_importer_gui] Camera saved and focused on preview (0, 0, 0).
[robot_importer_gui] Final spawn: entity='importer_test_box_1' ...
[robot_importer_gui] Spawn complete: importer_test_box_1
[robot_importer_gui] Camera pose restored.
```

- [ ] No `[Err]` lines from the importer itself
- [ ] No `Error Code 8` (auto-inertia without collision) — test box has explicit inertia
- [ ] Gazebo engine warnings (if any) come from the model/engine, not the importer

---

## 6. Known limitations (document, do not fix for MVP)

These are expected and acceptable for the MVP. Document them before moving to a complex model:

| Limitation | Impact | Mitigation |
|---|---|---|
| `set_pose` only moves if UserCommands system is in the world | Pose move fails silently for custom worlds without UserCommands | Log shows `set_pose failed` — use empty.sdf which includes UserCommands |
| Preview transparency applies only to visuals with SDF `<material>` blocks | Mesh-based visuals (DAE/OBJ) retain full opacity in preview | Acceptable for MVP; note in complex model test |
| Camera focus offset is fixed (–5 m along X, +2 m Z) | Non-ideal angle for some robot orientations | User can orbit with mouse |
| No XACRO argument UI | XACRO files with required args fail at expansion | Out of scope for MVP; show clear error in panel |
| Plugin rewriting is name-only (instanceName) | Topic/service names inside plugin elements still hardcoded | Documented limitation; user must handle plugin conflicts |
| World pause is never auto-resumed | User must press Play manually after import | Intentional — matches "Simulation paused" indicator |

---

## 7. Readiness gate for complex model

All items in sections 1–5 must pass before testing with a complex model (e.g. a full URDF robot with meshes and ROS plugins). The known limitations in section 6 should be acknowledged and understood.
