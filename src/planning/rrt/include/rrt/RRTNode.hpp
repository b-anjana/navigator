#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <rclcpp/rclcpp.hpp>
#include <vector>
#include <set>
#include <ctime>
#include <cstdlib>
#include <utility>
#include <random>
#include<cmath>
#include <limits>

// libOpenDRIVE stuff
#include "OpenDriveMap.h"
#include "Lanes.h"
#include "Road.h"
#include "Geometries/Line.h"
#include "RefLine.h"
#include "opendrive_utils/OpenDriveUtils.hpp"

// Message headers
#include <geometry_msgs/msg/point.hpp>
#include <geometry_msgs/msg/pose.hpp>
#include <geometry_msgs/msg/quaternion.hpp>
#include <geometry_msgs/msg/vector3.hpp>
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <std_msgs/msg/header.hpp>
#include <nav_msgs/msg/path.hpp>
#include <builtin_interfaces/msg/time.hpp>
#include <voltron_msgs/msg/egma.hpp>
#include <voltron_msgs/msg/evidential_grid.hpp>
#include <voltron_msgs/msg/evidential_grid_occupancy.hpp>
#include <voltron_msgs/msg/goal_position.hpp>
#include <voltron_msgs/msg/rrt_path.hpp>

class TreeNode{
    public:
        int x;
        int y;
        int index;
        bool goal;
        std::vector< TreeNode > children;
        TreeNode (){

        }
        TreeNode(int x, int y, int index) : x(x), y(y), index(index), goal(false) {}
};


class RRTNode : public rclcpp::Node {
	public:
		RRTNode();
		void findPath(voltron_msgs::msg::Egma::SharedPtr map);

	private:

		//class variables
		TreeNode *closest;
		TreeNode currentPosition;
		TreeNode goal;
		
		float maxDistanceToExplore = 3;
		float currentMinCostForSingleNode;
		float maxVelocity = 20;
		float tempPathCost;
		
		int iteration;
		int totalLeaves;

		std::pair<int,int> randomPoint;
        std::vector< TreeNode > finalRRTPath;

		//messages
		//voltron_msgs::msg::RrtPath path;
		//voltron_msgs::msg::Egma egma;
		//voltron_msgs::msg::GoalPosition goal_position;

		//publishers and subscribers
		//rclcpp::Publisher<voltron_msgs::msg::Egma>::SharedPtr fakeCostMapPub;
		//rclcpp::Publisher<voltron_msgs::msg::GoalPosition>::SharedPtr fakeGoalPup;
		//rclcpp::Publisher<voltron_msgs::msg::RrtPath>::SharedPtr rrt_path_pub;
		rclcpp::Publisher<nav_msgs::msg::Path>::SharedPtr rrt_path_publisher;
		rclcpp::Subscription<voltron_msgs::msg::GoalPosition>::SharedPtr goal_position_sub;
		rclcpp::Subscription<voltron_msgs::msg::Egma>::SharedPtr cost_map_sub;
		
		//methods to be called from find_path
		void createPaths(voltron_msgs::msg::Egma::SharedPtr map);
		void addNewRRTNode(voltron_msgs::msg::Egma::SharedPtr map);
		void findClosestState(TreeNode *head);
		void findRandomPair(int gridSize_x, int gridSize_y);
		void bestPath(TreeNode *head, float total, std::vector< TreeNode > tempPath, voltron_msgs::msg::Egma::SharedPtr map);

	
};