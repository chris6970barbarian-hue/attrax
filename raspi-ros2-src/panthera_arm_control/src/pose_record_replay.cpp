#include "rclcpp/rclcpp.hpp"

#include "example_interfaces/msg/bool.hpp"
#include "panthera_interfaces/srv/move_to_joint.hpp"
#include "sensor_msgs/msg/joint_state.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <vector>

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

class PoseRecordReplay : public rclcpp::Node
{
public:
    PoseRecordReplay()
        : Node("pose_record_replay")
    {
        sample_rate_ = declare_parameter<double>("sample_rate", 20.0);
        max_joint_step_ = declare_parameter<double>("max_joint_step", 0.01);
        velocity_scaling_ = declare_parameter<double>("velocity_scaling", 0.3);
        hold_time_ = declare_parameter<double>("hold_time", 0.5);
        command_interval_ = declare_parameter<double>("command_interval", 0.2);
        direct_waypoints_ = declare_parameter<bool>("direct_waypoints", true);
        wait_for_result_ = declare_parameter<bool>("wait_for_result", false);
        auto_enable_on_replay_ = declare_parameter<bool>("auto_enable_on_replay", false);
        save_file_ = declare_parameter<std::string>("save_file", "recorded_poses.csv");

        joint_sub_ = create_subscription<sensor_msgs::msg::JointState>(
            "joint_states_single", 10,
            std::bind(&PoseRecordReplay::jointCallback, this, std::placeholders::_1));
        move_to_joint_client_ = create_client<panthera_interfaces::srv::MoveToJoint>("move_to_joint");
        enable_pub_ = create_publisher<example_interfaces::msg::Bool>("enable_flag", 10);

        printHelp();
    }

    void waitForJointState()
    {
        RCLCPP_INFO(get_logger(), "Waiting for joint_states_single...");
        rclcpp::Rate rate(20.0);
        while (rclcpp::ok() && !have_joints_.load()) {
            rate.sleep();
        }
        printCurrentJoints("Current joints");
    }

    void keyboardLoop()
    {
        TerminalRawMode raw;
        if (!raw.enabled()) {
            RCLCPP_ERROR(get_logger(), "Keyboard input needs an interactive terminal.");
            return;
        }

        while (rclcpp::ok()) {
            char key = 0;
            if (read(raw.fd(), &key, 1) != 1) {
                continue;
            }

            if (key == 'q' || key == 'Q' || key == 3) {
                break;
            }
            if (key == 's' || key == 'S') {
                saveCurrentWaypoint();
            } else if (key == 'p' || key == 'P') {
                replayWaypoints();
            } else if (key == 'w' || key == 'W') {
                writeCsv();
            } else if (key == 'c' || key == 'C') {
                waypoints_.clear();
                RCLCPP_WARN(get_logger(), "Cleared recorded waypoints.");
            } else if (key == 'e' || key == 'E') {
                publishEnable(true);
            } else if (key == ' ') {
                publishEnable(false);
            } else if (key == 'h' || key == 'H') {
                printHelp();
            }
        }
        std::cout << "\nPose record/replay stopped.\n";
    }

private:
    void jointCallback(const sensor_msgs::msg::JointState::SharedPtr msg)
    {
        std::lock_guard<std::mutex> lock(joint_mutex_);
        if (msg->position.size() < 6) {
            return;
        }
        current_joints_.assign(msg->position.begin(), msg->position.begin() + 6);
        have_joints_ = true;
    }

    std::vector<double> currentJoints()
    {
        std::lock_guard<std::mutex> lock(joint_mutex_);
        return current_joints_;
    }

    void saveCurrentWaypoint()
    {
        auto joints = currentJoints();
        if (joints.size() != 6) {
            RCLCPP_WARN(get_logger(), "No valid joint state yet.");
            return;
        }
        waypoints_.push_back(joints);
        std::cout << "Saved waypoint " << waypoints_.size() << ": " << formatJoints(joints) << "\n";
    }

    void replayWaypoints()
    {
        if (waypoints_.size() < 2) {
            RCLCPP_WARN(get_logger(), "Need at least 2 waypoints. Press s at each manual pose first.");
            return;
        }

        if (auto_enable_on_replay_) {
            publishEnable(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }

        auto previous = currentJoints();
        if (previous.size() != 6) {
            RCLCPP_ERROR(get_logger(), "No valid current joint state before replay.");
            return;
        }

        RCLCPP_INFO(get_logger(), "Replaying %zu waypoints smoothly from current pose...", waypoints_.size());

        for (size_t waypoint_index = 0; waypoint_index < waypoints_.size(); ++waypoint_index) {
            const auto & waypoint = waypoints_[waypoint_index];
            const auto segment = direct_waypoints_
                ? std::vector<std::vector<double>>{waypoint}
                : interpolateSegment(previous, waypoint);
            for (const auto & joints : segment) {
                if (!callMoveToJoint(joints)) {
                    RCLCPP_ERROR(get_logger(), "Replay aborted at waypoint segment %zu", waypoint_index);
                    return;
                }
                std::this_thread::sleep_for(std::chrono::duration<double>(command_interval_));
                if (!rclcpp::ok()) {
                    return;
                }
            }
            previous = waypoint;
            std::this_thread::sleep_for(std::chrono::duration<double>(hold_time_));
        }
        RCLCPP_INFO(get_logger(), "Replay complete.");
    }

    std::vector<std::vector<double>> interpolateSegment(const std::vector<double> & start,
                                                        const std::vector<double> & goal) const
    {
        double max_delta = 0.0;
        for (size_t i = 0; i < 6; ++i) {
            max_delta = std::max(max_delta, std::abs(goal[i] - start[i]));
        }

        const int step_count = std::max(2, static_cast<int>(std::ceil(max_delta / max_joint_step_)));
        std::vector<std::vector<double>> segment;
        segment.reserve(step_count);

        for (int step = 1; step <= step_count; ++step) {
            const double phase = static_cast<double>(step) / step_count;
            const double smooth = 0.5 - 0.5 * std::cos(M_PI * phase);
            std::vector<double> joints(6, 0.0);
            for (size_t i = 0; i < 6; ++i) {
                joints[i] = start[i] + smooth * (goal[i] - start[i]);
            }
            segment.push_back(joints);
        }
        return segment;
    }

    bool callMoveToJoint(const std::vector<double> & joints)
    {
        if (!move_to_joint_client_->wait_for_service(std::chrono::seconds(2))) {
            RCLCPP_ERROR(get_logger(), "move_to_joint service is not available. Is arm_control_node running?");
            return false;
        }

        auto request = std::make_shared<panthera_interfaces::srv::MoveToJoint::Request>();
        for (size_t i = 0; i < 6; ++i) {
            request->joint_angles[i] = joints[i];
        }
        request->velocity_scaling = velocity_scaling_;
        request->acceleration_scaling = 1.0;

        auto future = move_to_joint_client_->async_send_request(request);
        if (!wait_for_result_) {
            return true;
        }

        const auto status = future.wait_for(std::chrono::seconds(20));
        if (status != std::future_status::ready) {
            RCLCPP_ERROR(get_logger(), "move_to_joint service timed out for %s", formatJoints(joints).c_str());
            return false;
        }

        auto response = future.get();
        if (!response->success) {
            RCLCPP_ERROR(get_logger(), "move_to_joint failed: %s", response->message.c_str());
            return false;
        }
        return true;
    }

    void publishEnable(bool enabled)
    {
        auto msg = example_interfaces::msg::Bool();
        msg.data = enabled;
        enable_pub_->publish(msg);
        RCLCPP_WARN(get_logger(), "Published enable_flag=%s", enabled ? "true" : "false");
    }

    void writeCsv()
    {
        std::ofstream file(save_file_);
        if (!file) {
            RCLCPP_ERROR(get_logger(), "Could not write %s", save_file_.c_str());
            return;
        }
        file << "joint1,joint2,joint3,joint4,joint5,joint6\n";
        file << std::fixed << std::setprecision(9);
        for (const auto & waypoint : waypoints_) {
            for (size_t i = 0; i < waypoint.size(); ++i) {
                if (i > 0) file << ',';
                file << waypoint[i];
            }
            file << '\n';
        }
        RCLCPP_INFO(get_logger(), "Saved %zu waypoints to %s", waypoints_.size(), save_file_.c_str());
    }

    std::string formatJoints(const std::vector<double> & joints) const
    {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(3) << '[';
        for (size_t i = 0; i < joints.size(); ++i) {
            if (i > 0) stream << ", ";
            stream << joints[i];
        }
        stream << ']';
        return stream.str();
    }

    void printCurrentJoints(const char * label)
    {
        std::cout << label << ": " << formatJoints(currentJoints()) << "\n";
    }

    void printHelp()
    {
        std::cout << "\nPanthera pose record/replay\n"
                  << "  Manually move the arm, then press s to save the current joint pose.\n"
                  << "  s: save waypoint, p: replay smoothly, w: write CSV, c: clear\n"
                  << "  e: enable arm, space: disable arm, h: help, q: quit\n"
                  << "  sample_rate=" << sample_rate_ << " Hz, max_joint_step=" << max_joint_step_
                  << " rad, velocity_scaling=" << velocity_scaling_
                  << ", direct_waypoints=" << (direct_waypoints_ ? "true" : "false")
                  << ", wait_for_result=" << (wait_for_result_ ? "true" : "false")
                  << ", command_interval=" << command_interval_ << " s\n\n";
    }

    rclcpp::Subscription<sensor_msgs::msg::JointState>::SharedPtr joint_sub_;
    rclcpp::Client<panthera_interfaces::srv::MoveToJoint>::SharedPtr move_to_joint_client_;
    rclcpp::Publisher<example_interfaces::msg::Bool>::SharedPtr enable_pub_;

    std::mutex joint_mutex_;
    std::vector<double> current_joints_;
    std::atomic<bool> have_joints_{false};
    std::vector<std::vector<double>> waypoints_;

    double sample_rate_{20.0};
    double max_joint_step_{0.01};
    double velocity_scaling_{0.3};
    double hold_time_{0.5};
    double command_interval_{0.2};
    bool direct_waypoints_{true};
    bool wait_for_result_{false};
    bool auto_enable_on_replay_{false};
    std::string save_file_;
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

    auto node = std::make_shared<PoseRecordReplay>();
    std::thread spin_thread([node]() { rclcpp::spin(node); });

    node->waitForJointState();
    if (running) {
        node->keyboardLoop();
    }

    rclcpp::shutdown();
    if (spin_thread.joinable()) {
        spin_thread.join();
    }
    return 0;
}
