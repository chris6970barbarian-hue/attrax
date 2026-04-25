from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument('sample_rate', default_value='20.0',
                              description='Replay command publish rate in Hz'),
        DeclareLaunchArgument('max_joint_step', default_value='0.2',
                              description='Maximum joint-space interpolation step in radians'),
        DeclareLaunchArgument('velocity_scaling', default_value='1.0',
                              description='move_to_joint velocity scaling for each interpolated point'),
        DeclareLaunchArgument('hold_time', default_value='0.0',
                              description='Pause time at each recorded waypoint in seconds'),
        DeclareLaunchArgument('command_interval', default_value='0.3',
                              description='Seconds to wait after sending each replay command'),
        DeclareLaunchArgument('direct_waypoints', default_value='true',
                              description='Move directly between recorded waypoints without interpolation'),
        DeclareLaunchArgument('wait_for_result', default_value='false',
                              description='Wait for each move_to_joint response before sending next command'),
        DeclareLaunchArgument('auto_enable_on_replay', default_value='false',
                              description='Publish enable_flag=true before replay'),
        DeclareLaunchArgument('save_file', default_value='recorded_poses.csv',
                              description='CSV path for saved waypoints'),

        Node(
            package='panthera_arm_control',
            executable='pose_record_replay',
            name='pose_record_replay',
            output='screen',
            emulate_tty=True,
            parameters=[{
                'sample_rate': LaunchConfiguration('sample_rate'),
                'max_joint_step': LaunchConfiguration('max_joint_step'),
                'velocity_scaling': LaunchConfiguration('velocity_scaling'),
                'hold_time': LaunchConfiguration('hold_time'),
                'command_interval': LaunchConfiguration('command_interval'),
                'direct_waypoints': LaunchConfiguration('direct_waypoints'),
                'wait_for_result': LaunchConfiguration('wait_for_result'),
                'auto_enable_on_replay': LaunchConfiguration('auto_enable_on_replay'),
                'save_file': LaunchConfiguration('save_file'),
            }],
        ),
    ])
