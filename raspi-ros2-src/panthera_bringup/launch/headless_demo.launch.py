from launch import LaunchDescription
from launch.actions import RegisterEventHandler, Shutdown
from launch.event_handlers import OnProcessExit
from launch.substitutions import Command, FindExecutable
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from ament_index_python.packages import get_package_share_directory
import os


def generate_launch_description():
    moveit_config_pkg = get_package_share_directory('with_gripper_config')

    urdf_file = os.path.join(
        moveit_config_pkg, 'config', 'Panthera-HT_description.urdf.xacro'
    )
    initial_positions_file = os.path.join(
        moveit_config_pkg, 'config', 'initial_positions.yaml'
    )

    robot_description_content = Command(
        [
            FindExecutable(name='xacro'),
            ' ',
            urdf_file,
            ' initial_positions_file:=',
            initial_positions_file,
        ]
    )
    robot_description = {
        'robot_description': ParameterValue(robot_description_content, value_type=str)
    }

    robot_state_publisher_node = Node(
        package='robot_state_publisher',
        executable='robot_state_publisher',
        name='robot_state_publisher',
        output='screen',
        parameters=[robot_description],
    )

    demo_node = Node(
        package='panthera_bringup',
        executable='headless_arm_motion_demo.py',
        name='headless_arm_motion_demo',
        output='screen',
    )

    shutdown_when_demo_exits = RegisterEventHandler(
        OnProcessExit(
            target_action=demo_node,
            on_exit=[Shutdown(reason='Headless arm demo finished')],
        )
    )

    return LaunchDescription(
        [robot_state_publisher_node, demo_node, shutdown_when_demo_exits]
    )
