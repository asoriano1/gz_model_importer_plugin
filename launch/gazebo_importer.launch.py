#!/usr/bin/env python3

import os

from ament_index_python.packages import PackageNotFoundError, get_package_prefix
from launch import LaunchDescription
from launch.actions import (
    DeclareLaunchArgument,
    ExecuteProcess,
    LogInfo,
    OpaqueFunction,
    SetEnvironmentVariable,
)
from launch.substitutions import EnvironmentVariable, LaunchConfiguration


PACKAGE_NAME = "gz_model_importer"


def _as_bool(value: str) -> bool:
    return value.strip().lower() in ("1", "true", "yes", "on")


def _launch_setup(context, *args, **kwargs):
    importer_prefix = get_package_prefix(PACKAGE_NAME)
    importer_share_dir = os.path.join(importer_prefix, "share", PACKAGE_NAME)
    importer_lib_dir = os.path.join(importer_prefix, "lib")
    gui_config_path = os.path.join(
        importer_share_dir, "config", "gz_model_importer.config"
    )
    default_world_path = os.path.join(
        importer_share_dir, "worlds", "robotnik_world.sdf"
    )

    world_arg = LaunchConfiguration("world").perform(context).strip()
    gui_enabled = _as_bool(LaunchConfiguration("gui").perform(context))
    start_paused = _as_bool(LaunchConfiguration("paused").perform(context))
    bridge_clock = _as_bool(LaunchConfiguration("bridge_clock").perform(context))
    render_engine = LaunchConfiguration("render_engine").perform(context).strip()

    actions = [
        LogInfo(msg=f"Model Importer lib dir: {importer_lib_dir}"),
        LogInfo(msg=f"GUI config path: {gui_config_path}"),
        SetEnvironmentVariable(
            name="GZ_GUI_PLUGIN_PATH",
            value=[
                importer_lib_dir,
                ":",
                EnvironmentVariable("GZ_GUI_PLUGIN_PATH", default_value=""),
            ],
        ),
    ]

    try:
        gz_ros2_control_prefix = get_package_prefix("gz_ros2_control")
        gz_ros2_control_lib_dir = os.path.join(gz_ros2_control_prefix, "lib")
        actions.extend(
            [
                LogInfo(msg=f"gz_ros2_control lib dir: {gz_ros2_control_lib_dir}"),
                SetEnvironmentVariable(
                    name="GZ_SIM_SYSTEM_PLUGIN_PATH",
                    value=[
                        gz_ros2_control_lib_dir,
                        ":",
                        EnvironmentVariable(
                            "GZ_SIM_SYSTEM_PLUGIN_PATH", default_value=""
                        ),
                    ],
                ),
            ]
        )
    except PackageNotFoundError:
        actions.append(
            LogInfo(
                msg=(
                    "gz_ros2_control package was not found.\n"
                    "Models requiring libgz_ros2_control-system.so may fail to load.\n"
                    "Install ros-jazzy-gz-ros2-control or source the workspace "
                    "containing gz_ros2_control."
                )
            )
        )

    if bridge_clock:
        try:
            ros_gz_bridge_prefix = get_package_prefix("ros_gz_bridge")
            clock_bridge_exec = os.path.join(
                ros_gz_bridge_prefix, "lib", "ros_gz_bridge", "parameter_bridge"
            )
            if os.path.exists(clock_bridge_exec):
                actions.extend(
                    [
                        LogInfo(msg=f"ros_gz_bridge clock bridge: {clock_bridge_exec}"),
                        ExecuteProcess(
                            cmd=[
                                clock_bridge_exec,
                                "/clock@rosgraph_msgs/msg/Clock[gz.msgs.Clock",
                            ],
                            output="screen",
                        ),
                    ]
                )
            else:
                actions.append(
                    LogInfo(
                        msg=(
                            "ros_gz_bridge parameter_bridge executable was not found. "
                            "ROS nodes using use_sim_time may warn until /clock is bridged."
                        )
                    )
                )
        except PackageNotFoundError:
            actions.append(
                LogInfo(
                    msg=(
                        "ros_gz_bridge package was not found. "
                        "ROS nodes using use_sim_time, including controller_manager "
                        "from gz_ros2_control, may warn until /clock is bridged."
                    )
                )
            )

    world_path = os.path.expanduser(world_arg) if world_arg else default_world_path
    if world_arg:
        actions.append(LogInfo(msg=f"Gazebo world: {world_path}"))
    elif os.path.exists(default_world_path):
        actions.append(LogInfo(msg=f"Gazebo world: {default_world_path}"))
    else:
        world_path = ""
        actions.append(
            LogInfo(
                msg=(
                    "No package default world was found. "
                    "Launching Gazebo without an explicit world file."
                )
            )
        )

    cmd = ["gz", "sim", "-v", LaunchConfiguration("verbose")]
    if not start_paused:
        cmd.append("-r")

    if gui_enabled:
        cmd.extend(["--gui-config", gui_config_path])
    else:
        cmd.append("-s")

    if render_engine:
        cmd.extend(["--render-engine", render_engine])

    if world_path:
        cmd.append(world_path)

    actions.append(ExecuteProcess(cmd=cmd, output="screen"))
    return actions


def generate_launch_description():
    return LaunchDescription(
        [
            DeclareLaunchArgument(
                "world",
                default_value="",
                description=(
                    "Absolute or relative path to the world SDF to load. "
                    "If empty, the package default world is used."
                ),
            ),
            DeclareLaunchArgument(
                "verbose",
                default_value="4",
                description="Gazebo console verbosity level (0-4).",
            ),
            DeclareLaunchArgument(
                "gui",
                default_value="true",
                description=(
                    "Launch the Gazebo GUI and load the Model Importer panel. "
                    "Set to false to run server-only mode."
                ),
            ),
            DeclareLaunchArgument(
                "paused",
                default_value="false",
                description="Start the simulation paused when true.",
            ),
            DeclareLaunchArgument(
                "bridge_clock",
                default_value="true",
                description=(
                    "Start a ros_gz_bridge /clock bridge when ros_gz_bridge is "
                    "available so ROS nodes can use sim time."
                ),
            ),
            DeclareLaunchArgument(
                "render_engine",
                default_value="",
                description=(
                    "Optional Gazebo render engine override, for example "
                    "'ogre' or 'ogre2'."
                ),
            ),
            OpaqueFunction(function=_launch_setup),
        ]
    )
