#include "rclcpp/rclcpp.hpp"

#include "example_interfaces/msg/bool.hpp"
#include "panthera_interfaces/msg/end_pose_euler.hpp"
#include "panthera_interfaces/msg/pos_cmd.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <fcntl.h>
#include <termios.h>
#include <thread>
#include <unistd.h>

using namespace std::chrono_literals;

class TerminalRawMode
{
public:
    TerminalRawMode()
    {
        fd_ = open("/dev/tty", O_RDONLY);
        if (fd_ < 0 && isatty(STDIN_FILENO)) {
            fd_ = STDIN_FILENO;
        }

        enabled_ = fd_ >= 0 && tcgetattr(fd_, &original_) == 0;
        if (!enabled_) {
            return;
        }

        termios raw = original_;
        raw.c_lflag &= static_cast<unsigned int>(~(ICANON | ECHO));
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(fd_, TCSANOW, &raw);
    }

    ~TerminalRawMode()
    {
        if (enabled_) {
            tcsetattr(fd_, TCSANOW, &original_);
        }
        if (fd_ >= 0 && fd_ != STDIN_FILENO) {
            close(fd_);
        }
    }

    bool enabled() const { return enabled_; }
    int fd() const { return fd_; }

private:
    termios original_{};
    int fd_{-1};
    bool enabled_{false};
};

class WasdTipTeleop : public rclcpp::Node
{
public:
    WasdTipTeleop()
        : Node("wasd_tip_teleop")
    {
        linear_step_ = declare_parameter<double>("linear_step", 0.001);
        angular_step_ = declare_parameter<double>("angular_step", 0.01);
        min_command_interval_ = declare_parameter<double>("min_command_interval", 0.25);
        gripper_ = declare_parameter<double>("initial_gripper", -1.0);
        mode1_ = declare_parameter<int>("mode1", 0);
        auto_enable_ = declare_parameter<bool>("auto_enable", false);

        pos_pub_ = create_publisher<panthera_interfaces::msg::PosCmd>("pos_cmd", 10);
        enable_pub_ = create_publisher<example_interfaces::msg::Bool>("enable_flag", 10);
        pose_sub_ = create_subscription<panthera_interfaces::msg::EndPoseEuler>(
            "end_pose_euler", 10,
            std::bind(&WasdTipTeleop::poseCallback, this, std::placeholders::_1));

        if (auto_enable_) {
            auto msg = example_interfaces::msg::Bool();
            msg.data = true;
            enable_pub_->publish(msg);
        }

        printHelp();
    }

    bool havePose() const { return have_pose_.load(); }

    void waitForFirstPose()
    {
        RCLCPP_INFO(get_logger(), "Waiting for first end_pose_euler message...");
        rclcpp::Rate rate(20.0);
        while (rclcpp::ok() && !havePose()) {
            rate.sleep();
        }
        printPose("Start pose");
    }

    void runKeyboardLoop()
    {
        TerminalRawMode raw_mode;
        if (!raw_mode.enabled()) {
            RCLCPP_ERROR(get_logger(), "Keyboard input needs an interactive terminal.");
            return;
        }

        while (rclcpp::ok()) {
            char key = 0;
            if (read(raw_mode.fd(), &key, 1) != 1) {
                continue;
            }

            if (key == 'q' || key == 'Q' || key == 3) {
                break;
            }

            handleKey(key);
        }

        std::cout << "\nTeleop stopped.\n";
    }

private:
    void poseCallback(const panthera_interfaces::msg::EndPoseEuler::SharedPtr msg)
    {
        std::lock_guard<std::mutex> lock(pose_mutex_);
        current_pose_ = *msg;
        have_pose_ = true;
        if (!have_target_) {
            target_pose_ = *msg;
            have_target_ = true;
        }
    }

    void handleKey(char key)
    {
        panthera_interfaces::msg::EndPoseEuler target;
        {
            std::lock_guard<std::mutex> lock(pose_mutex_);
            if (!have_target_) {
                RCLCPP_WARN(get_logger(), "No pose feedback yet; ignoring key.");
                return;
            }
            target = target_pose_;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - last_command_time_ < std::chrono::duration<double>(min_command_interval_)) {
            RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 1000,
                                 "Command throttled; press keys more slowly or lower min_command_interval.");
            return;
        }

        bool publish = true;
        switch (key) {
            case 'w': target.x += linear_step_; break;
            case 's': target.x -= linear_step_; break;
            case 'a': target.y += linear_step_; break;
            case 'd': target.y -= linear_step_; break;
            case 'r': target.z += linear_step_; break;
            case 'f': target.z -= linear_step_; break;
            case 'j': target.yaw += angular_step_; break;
            case 'l': target.yaw -= angular_step_; break;
            case 'i': target.pitch += angular_step_; break;
            case 'k': target.pitch -= angular_step_; break;
            case 'u': target.roll += angular_step_; break;
            case 'o': target.roll -= angular_step_; break;
            case '[': gripper_ = 0.0; break;
            case ']': gripper_ = 1.0; break;
            case ' ': publishStopEnable(); publish = false; break;
            case 'h':
            case 'H': printHelp(); publish = false; break;
            default: publish = false; break;
        }

        if (!publish) {
            return;
        }

        auto cmd = panthera_interfaces::msg::PosCmd();
        cmd.x = target.x;
        cmd.y = target.y;
        cmd.z = target.z;
        cmd.roll = target.roll;
        cmd.pitch = target.pitch;
        cmd.yaw = target.yaw;
        cmd.gripper = gripper_;
        cmd.mode1 = static_cast<uint8_t>(mode1_);
        cmd.mode2 = 0;
        pos_pub_->publish(cmd);
        last_command_time_ = now;

        {
            std::lock_guard<std::mutex> lock(pose_mutex_);
            target_pose_ = target;
        }

        std::cout << std::fixed << std::setprecision(3)
                  << "target xyz=[" << cmd.x << ", " << cmd.y << ", " << cmd.z
                  << "] rpy=[" << cmd.roll << ", " << cmd.pitch << ", " << cmd.yaw
                  << "] gripper=" << cmd.gripper << "\r" << std::flush;
    }

    void publishStopEnable()
    {
        auto msg = example_interfaces::msg::Bool();
        msg.data = false;
        enable_pub_->publish(msg);
        RCLCPP_WARN(get_logger(), "Published enable_flag=false. Press Ctrl+C and restart arm_control if needed.");
    }

    void printHelp()
    {
        std::cout << "\nPanthera WASD tip teleop\n"
                  << "  w/s: +X/-X tip movement\n"
                  << "  a/d: +Y/-Y tip movement\n"
                  << "  r/f: +Z/-Z tip movement\n"
                  << "  u/o: +roll/-roll, i/k: +pitch/-pitch, j/l: +yaw/-yaw\n"
                  << "  [/]: close/open gripper on next command\n"
                  << "  space: disable arm, h: help, q: quit\n"
                  << "  linear_step=" << linear_step_ << " m, angular_step=" << angular_step_
                  << " rad, min_command_interval=" << min_command_interval_ << " s\n\n";
    }

    void printPose(const char * label)
    {
        std::lock_guard<std::mutex> lock(pose_mutex_);
        std::cout << std::fixed << std::setprecision(3)
                  << label << ": xyz=[" << current_pose_.x << ", " << current_pose_.y << ", " << current_pose_.z
                  << "] rpy=[" << current_pose_.roll << ", " << current_pose_.pitch << ", " << current_pose_.yaw
                  << "]\n";
    }

    rclcpp::Publisher<panthera_interfaces::msg::PosCmd>::SharedPtr pos_pub_;
    rclcpp::Publisher<example_interfaces::msg::Bool>::SharedPtr enable_pub_;
    rclcpp::Subscription<panthera_interfaces::msg::EndPoseEuler>::SharedPtr pose_sub_;

    mutable std::mutex pose_mutex_;
    panthera_interfaces::msg::EndPoseEuler current_pose_{};
    panthera_interfaces::msg::EndPoseEuler target_pose_{};
    std::atomic<bool> have_pose_{false};
    bool have_target_{false};
    std::chrono::steady_clock::time_point last_command_time_{};

    double linear_step_{0.01};
    double angular_step_{0.05};
    double min_command_interval_{0.25};
    double gripper_{-1.0};
    int mode1_{0};
    bool auto_enable_{true};
};

std::atomic<bool> running{true};

void signalHandler(int)
{
    running = false;
    rclcpp::shutdown();
}

int main(int argc, char ** argv)
{
    rclcpp::init(argc, argv);
    std::signal(SIGINT, signalHandler);

    auto node = std::make_shared<WasdTipTeleop>();
    std::thread spin_thread([node]() { rclcpp::spin(node); });

    node->waitForFirstPose();
    if (running) {
        node->runKeyboardLoop();
    }

    rclcpp::shutdown();
    if (spin_thread.joinable()) {
        spin_thread.join();
    }
    return 0;
}
