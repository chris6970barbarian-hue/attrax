from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument('linear_step', default_value='0.001',
                              description='Tip translation per keypress in meters'),
        DeclareLaunchArgument('angular_step', default_value='0.01',
                              description='Tip orientation step per keypress in radians'),
        DeclareLaunchArgument('min_command_interval', default_value='0.25',
                              description='Minimum seconds between published movement commands'),
        DeclareLaunchArgument('mode1', default_value='0',
                              description='0 = direct IK target, 1 = interpolated Cartesian path'),
        DeclareLaunchArgument('auto_enable', default_value='false',
                              description='Publish enable_flag=true on startup'),

        Node(
            package='panthera_arm_control',
            executable='wasd_tip_teleop',
            name='wasd_tip_teleop',
            output='screen',
            emulate_tty=True,
            parameters=[{
                'linear_step': LaunchConfiguration('linear_step'),
                'angular_step': LaunchConfiguration('angular_step'),
                'min_command_interval': LaunchConfiguration('min_command_interval'),
                'mode1': LaunchConfiguration('mode1'),
                'auto_enable': LaunchConfiguration('auto_enable'),
            }],
        ),
    ])
