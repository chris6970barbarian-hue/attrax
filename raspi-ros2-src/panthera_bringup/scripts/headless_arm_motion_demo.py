#!/usr/bin/env python3

import math

import rclpy
from rclpy.node import Node
from sensor_msgs.msg import JointState


class HeadlessArmMotionDemo(Node):
    def __init__(self) -> None:
        super().__init__('headless_arm_motion_demo')
        self.publisher = self.create_publisher(JointState, '/joint_states', 10)
        self.start_time = self.get_clock().now()
        self.last_log_second = -1
        self.duration_sec = 8.0
        self.angular_frequency = 2.0 * math.pi * 0.25
        self.joint_names = [
            'joint1',
            'joint2',
            'joint3',
            'joint4',
            'joint5',
            'joint6',
            'L_finger_joint',
        ]
        self.timer = self.create_timer(1.0 / 30.0, self.publish_joint_state)
        self.get_logger().info('Starting Panthera headless arm motion demo')

    def publish_joint_state(self) -> None:
        elapsed = (
            self.get_clock().now() - self.start_time
        ).nanoseconds / 1_000_000_000.0

        if elapsed > self.duration_sec:
            self.get_logger().info('Headless arm motion demo completed')
            self.timer.cancel()
            rclpy.shutdown()
            return

        phase = self.angular_frequency * elapsed
        positions = [
            0.35 * math.sin(phase),
            0.85 + 0.35 * math.sin(phase),
            0.95 + 0.30 * math.sin(phase + math.pi / 4.0),
            0.25 + 0.15 * math.sin(phase + math.pi / 2.0),
            -0.20 + 0.18 * math.sin(phase + math.pi / 6.0),
            0.30 * math.sin(phase + math.pi / 3.0),
            0.0,
        ]

        msg = JointState()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.name = self.joint_names
        msg.position = positions
        self.publisher.publish(msg)

        current_second = int(elapsed)
        if current_second != self.last_log_second:
            self.last_log_second = current_second
            summary = ', '.join(f'{value:.2f}' for value in positions[:6])
            self.get_logger().info(f't={elapsed:.1f}s joints=[{summary}]')


def main() -> None:
    rclpy.init()
    node = HeadlessArmMotionDemo()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        if rclpy.ok():
            rclpy.shutdown()
        node.destroy_node()


if __name__ == '__main__':
    main()
