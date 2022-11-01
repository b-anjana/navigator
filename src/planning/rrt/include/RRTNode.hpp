#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <rclcpp/rclcpp.hpp>
#include <vector>
#include <set>

// Message headers
#include <geometry_msgs/msg/point.hpp>
#include <geometry_msgs/msg/vector3.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <std_msgs/msg/color_rgba.hpp>
#include <visualization_msgs/msg/marker.hpp>
#include <visualization_msgs/msg/marker_array.hpp>
#include <voltron_msgs/msg/final_path.hpp>

class WayPointRRT{
public:
	float velocity;
	float gps_X;
	float gps_Y;
	WayPointRRT(float velocity, float gps_X, float gps_Y){
		this.velocity = velocity;
		this.gps_X = gps_X;
		this.gps_Y = gps_Y;
	}


}

class WayPointPath {
public:
	std::vector<WayPointRRT> path;
	WayPointPath(std::vector<WayPointRRT> tempPath){
		path = tempPath;
	}

}

class RRTNode : public rclcpp::Node {
public:
	RRTNode();
	WayPointPath createTree(); //TODO: check for costMap input for parameter
}
