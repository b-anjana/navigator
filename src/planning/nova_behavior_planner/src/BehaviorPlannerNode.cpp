#include "nova_behavior_planner/BehaviorPlannerNode.hpp"
#include <chrono>
#include <memory>
#include <iostream>
#include <tf2/utils.h>
#include <cmath>
#include <unordered_set>
#include <boost/geometry/algorithms/within.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/geometries/polygon.hpp>
#include <boost/geometry/algorithms/assign.hpp>

using namespace std::chrono_literals;
using std::placeholders::_1;

#define MAP_PARAM "map_xodr_file_path"
#define STOP_TICK "stop_tick"
#define JUNC_DELAY "junc_delay"
#define MAX_DIST_TO_ZONE "max_dist_to_zone"
#define MIN_DIST_OUT_ZONE "min_dist_out_zone"
#define STOP_SPEED_DIF "stop_speed_dif"
#define STOP_POSE_DIF "stop_pose_dif"

using namespace Nova::BehaviorPlanner;

BehaviorPlannerNode::BehaviorPlannerNode() : rclcpp::Node("behavior_planner") {  

  this->current_state = LANEKEEPING;
  this->reached_zone = false;
  this->stop_ticks = 0;
  this->yield_ticks = 0;
  this->delay_ticks = 0;

  this->declare_parameter<std::string>(MAP_PARAM, "data/maps/grand_loop/grand_loop.xodr");
	this->declare_parameter<int>(STOP_TICK, 10);
  this->declare_parameter<int>(JUNC_DELAY, 20);
  this->declare_parameter<double>(MAX_DIST_TO_ZONE, 2.0);
  this->declare_parameter<double>(MIN_DIST_OUT_ZONE, 10.0);
  this->declare_parameter<double>(STOP_SPEED_DIF, 0.25);
  this->declare_parameter<double>(STOP_POSE_DIF, 1.0);

  std::string xodr_path;
	this->get_parameter<std::string>(MAP_PARAM, xodr_path);
	this->get_parameter<int>(STOP_TICK, max_stop_ticks);
  this->get_parameter<int>(JUNC_DELAY, max_junc_delay);
  this->get_parameter<double>(MAX_DIST_TO_ZONE, max_dist_to_zone);
  this->get_parameter<double>(MIN_DIST_OUT_ZONE, min_dist_out_zone);
  this->get_parameter<double>(STOP_SPEED_DIF, stop_speed_dif);
  this->get_parameter<double>(STOP_POSE_DIF, stop_pose_dif);

    // xml parsing
  RCLCPP_INFO(this->get_logger(), "Reading from " + xodr_path);
  std::shared_ptr<navigator::opendrive::MapInfo> map_info = navigator::opendrive::load_map(xodr_path);
  this->map = map_info->map;
  this->map_info = map_info;

  this->control_timer = this->create_wall_timer(message_frequency, std::bind(&BehaviorPlannerNode::send_message, this));

  this->final_zone_publisher = this->create_publisher<ZoneArray>("zone_array", 10);

  this->current_state_publisher = this->create_publisher<BehaviorState>("current_state", 10);

  this->odometry_subscription = this->create_subscription<Odometry>("/gnss_odom", 8, std::bind(&BehaviorPlannerNode::update_current_speed, this, _1));

  this->path_subscription = this->create_subscription<FinalPath>("paths", 8, std::bind(&BehaviorPlannerNode::update_current_path, this, _1));

  this->obstacles_subscription = this->create_subscription<Obstacles>("/sensors/zed/obstacle_array_3d", 8, std::bind(&BehaviorPlannerNode::update_current_obstacles, this, _1));

  this->button_subscription = this->create_subscription<Bool>("joy_control_junction_enable", 8, std::bind(&BehaviorPlannerNode::update_button, this, _1));

  this->tf_buffer_ = std::make_unique<tf2_ros::Buffer>(this->get_clock());
  this->transform_listener_ = std::make_shared<tf2_ros::TransformListener>(*tf_buffer_);
}

BehaviorPlannerNode::~BehaviorPlannerNode() {}

void BehaviorPlannerNode::update_current_speed(Odometry::SharedPtr ptr)
{
  // save current position into prev position and update value
  this->prev_position_x = this->current_position_x;
  this->prev_position_y = this->current_position_y;
  this->current_position_x = ptr->pose.pose.position.x;
  this->current_position_y = ptr->pose.pose.position.y;

  // calculate linear velocity (Z-axis eliminated due to unnecessary noise)
  tf2::Vector3 vector_velocity(ptr->twist.twist.linear.x, ptr->twist.twist.linear.y, 0.0);
  this->current_speed = std::sqrt(vector_velocity.dot(vector_velocity));
}

void BehaviorPlannerNode::update_current_path(FinalPath::SharedPtr ptr)
{
  this->current_path = ptr;
}

void BehaviorPlannerNode::update_current_obstacles(Obstacles::SharedPtr ptr)
{
  this->current_obstacles = ptr;
}

void BehaviorPlannerNode::update_button(Bool::SharedPtr ptr)
{
  button_pressed = ptr->data;
}

void BehaviorPlannerNode::update_tf()
{
  try
  {
    currentTf = tf_buffer_->lookupTransform("map", "base_link", tf2::TimePointZero);
  }
  catch (tf2::TransformException &e)
  {
    RCLCPP_INFO(this->get_logger(), "Could not transform base_link to map: %s", e.what());
    return;
  }
}

std::array<geometry_msgs::msg::Point, 8> BehaviorPlannerNode::transform_obstacle(const Obstacle &obstacle)
{
  double tf_x = this->currentTf.transform.translation.x;
  double tf_y = this->currentTf.transform.translation.y;
  std::array<geometry_msgs::msg::Point, 8> corners;

  // transform corner
  for (size_t i = 0; i < obstacle.bounding_box.corners.size(); i++)
  {
    corners[i].x = obstacle.bounding_box.corners[i].x + tf_x;
    corners[i].y = obstacle.bounding_box.corners[i].y + tf_y;
  }
  return corners;
}

void BehaviorPlannerNode::send_message()
{
  if (this->current_path == nullptr)
    return;
  update_state();
  final_zone_publisher->publish(this->final_zones);
  BehaviorState publish_state;
  publish_state.current_state = this->current_state;
  current_state_publisher->publish(publish_state);
}

void BehaviorPlannerNode::update_state()
{
  switch (current_state)
  {
  case LANEKEEPING:
    RCLCPP_INFO(this->get_logger(), "current state: LANEKEEPING");

    if (upcoming_intersection()) {
      if (final_zones.zones[0].max_speed == STOP_SPEED) {
        current_state = STOPPING;
      }
      else {
        current_state = YIELDING;
      }
    }
    break;
  case YIELDING:
    RCLCPP_INFO(this->get_logger(), "current state: YIELDING");

    if (reached_desired_velocity(YIELD_SPEED))
      yield_ticks += 1;
    if (yield_ticks >= 5) {
      yield_ticks = 0;
      current_state = IN_JUNCTION;
    }
    break;
  case STOPPING:
    RCLCPP_INFO(this->get_logger(), "current state: STOPPING");

    if (is_stopped())
      stop_ticks += 1;
    if (stop_ticks >= max_stop_ticks) {
      stop_ticks = 0;
      current_state = STOPPED;
    }
    break;
  case STOPPED:
    RCLCPP_INFO(this->get_logger(), "current state: STOPPED");

    if (!obstacles_present())
    {
      if (final_zones.zones.size())
      {
        // will set speed in_junction state anyways
        // final_zones.zones[0].max_speed = YIELD_SPEED;
        current_state = JUNCTION_MANUAL_DELAY;
      }
    }
    else if (obs_with_ROW.size() != 0)
    {
      current_state = WAITING_AT_JUNCTION;
    }
    break;
  case WAITING_AT_JUNCTION:
    RCLCPP_INFO(this->get_logger(), "current state: WAITING_AT_JUNCTION");

    if (obs_with_ROW.size() != 0)
    {
      // check if any of the ID obstacles are gone and remove from ID array
      check_right_of_way();
    }
    else
    {
      current_state = JUNCTION_MANUAL_DELAY;
    }
    break;
  case JUNCTION_MANUAL_DELAY:
    RCLCPP_INFO(this->get_logger(), "current state: Waiting for manual interference before entering junction");

    // if button pressed, then change to in_junction state
    if (button_pressed)
    {
      current_state = IN_JUNCTION;
    }
    break;
  case IN_JUNCTION:
    RCLCPP_INFO(this->get_logger(), "current state: IN_JUNCTION");

    if (obstacles_present(true) && final_zones.zones.size())
    {
      final_zones.zones[0].max_speed = STOP_SPEED;
      RCLCPP_INFO(this->get_logger(), "EMERGENCY STOP");
    }
    else if (final_zones.zones.size())
    {
      final_zones.zones[0].max_speed = YIELD_SPEED;
    }

    // to account for gap between car & zone
    if (point_in_zone(current_position_x, current_position_y))
      reached_zone = true;

    if (reached_zone)
    {
      if (!point_in_zone(current_position_x, current_position_y))
      {
        reached_zone = false;
        current_state = LANEKEEPING;
      }
    }
    break;
  }
}

float BehaviorPlannerNode::zone_point_distance(float x, float y)
{

  if (!final_zones.zones.size())
  {
    RCLCPP_INFO(this->get_logger(), "ERROR: in junction but no zones");
    return false;
  }

  auto lane = navigator::opendrive::get_lane_from_xy(map, x, y);

  // obstacle not on lane so is not ROW obstacle
  if (lane == nullptr)
    return 999;

  std::string road = lane->road.lock()->id;
  auto incoming_roads = navigator::opendrive::get_incoming_roads(map, junction_ids[0]);

  // road is not an incoming road of this junction
  if (incoming_roads.find(road) == incoming_roads.end())
    return 999;

  // valid ROW obstacle

  polygon_type zone_poly;
  point_type p(x, y);

  std::vector<point_type> poly_points;
  for (auto point : final_zones.zones[0].poly.points)
  {
    poly_points.push_back(point_type(point.x, point.y));
  }

  boost::geometry::assign_points(zone_poly, poly_points);
  return boost::geometry::distance(p, zone_poly);
}

bool BehaviorPlannerNode::point_in_zone(float x, float y)
{

  if (!final_zones.zones.size())
  {
    RCLCPP_INFO(this->get_logger(), "ERROR: in junction but no zones");
    return false;
  }

  polygon_type zone_poly;
  point_type p(x, y);

  std::vector<point_type> poly_points;
  for (auto point : final_zones.zones[0].poly.points)
  {
    poly_points.push_back(point_type(point.x, point.y));
  }

  boost::geometry::assign_points(zone_poly, poly_points);
  return boost::geometry::within(p, zone_poly);
}

bool BehaviorPlannerNode::poly_in_zone(BoundingBox obs_bbox)
{

  if (!final_zones.zones.size())
  {
    RCLCPP_INFO(this->get_logger(), "ERROR: in junction but no zones");
    return false;
  }

  polygon_type zone_poly;
  polygon_type obs_poly;

  std::vector<point_type> zone_points;
  for (auto point : final_zones.zones[0].poly.points)
  {
    zone_points.push_back(point_type(point.x, point.y));
  }
  boost::geometry::assign_points(zone_poly, zone_points);

  for (Point point : obs_bbox.corners)
  {
    if (boost::geometry::within(point_type(point.x, point.y), zone_poly))
    {
      return true;
    }
  }

  return false;
}

bool BehaviorPlannerNode::obstacles_present(bool in_junction)
{

  this->update_tf();
  bool obs_in_junction = false;

  for (Obstacle obs : current_obstacles->obstacles)
  {
    std::array<geometry_msgs::msg::Point, 8> corners = transform_obstacle(obs);

    size_t i = 0; // only use 4 corners (2D)
    for (const auto &point : corners)
    {
      if (i < 4 && point_in_zone(point.x, point.y))
      {
        // obstacle is already inside zone
        obs_in_junction = true;
      }
      else if (i < 4 && !in_junction && zone_point_distance(point.x, point.y) < max_dist_to_zone)
      {
        // obstacle is nearby zone (assume they have right-of-way)
        // don't care about this property if we are already inside junction
        obs_with_ROW.push_back(obs.id);
        break;
      }
      i += 1;
    }

    RCLCPP_INFO(this->get_logger(), std::to_string(obs_with_ROW.size()));
  }

  return obs_in_junction || (obs_with_ROW.size() != 0);
}

// TODO: switch from set distance to just checking if obstacle is gone from view
void BehaviorPlannerNode::check_right_of_way()
{

  this->update_tf();

  for (Obstacle obs : current_obstacles->obstacles)
  {

    std::array<geometry_msgs::msg::Point, 8> corners = transform_obstacle(obs);

    float min_distance = 1000;
    size_t i = 0; // last 4 corners of Carla are nonsense for some reason
    for (const auto &point : corners)
    {
      if (i >= 4)
        break;
      i += 1;
      float distance = zone_point_distance(point.x, point.y);
      if (distance < min_distance)
      {
        min_distance = distance;
      }
    }

    // if zone-closest corner is farther away than 10 meters, obstacle can be removed
    if (min_distance > min_dist_out_zone)
    {
      for (size_t i = 0; i < obs_with_ROW.size(); i++)
      {
        if (obs_with_ROW[i] == obs.id)
        {
          obs_with_ROW.erase(obs_with_ROW.begin() + i);
          break;
        }
      }
    }
  }
}

bool BehaviorPlannerNode::reached_desired_velocity(float desired_velocity)
{
  float speed_dif = std::abs(desired_velocity - current_speed);
  return speed_dif < 0.08;
}

bool BehaviorPlannerNode::is_stopped()
{
  float speed_dif = std::abs(STOP_SPEED - current_speed);
  float position_dif = std::abs(prev_position_x - current_position_x) + std::abs(prev_position_y - current_position_y);
  return (speed_dif < stop_speed_dif && position_dif < stop_pose_dif);
}

bool BehaviorPlannerNode::upcoming_intersection()
{

  using navigator::opendrive::Signal;
  bool zones_made = false;
  std::unordered_set<std::string> seen_junctions;
  final_zones.zones.clear();

  // find point on path closest to current location
  size_t closest_pt_idx = 0;
  float min_distance = -1;
  for (size_t i = 0; i < current_path->points.size(); i++)
  {
    float distance = pow(current_path->points[i].x - current_position_x, 2);
    distance += pow(current_path->points[i].y - current_position_y, 2);
    if (min_distance == -1 || distance < min_distance)
    {
      min_distance = distance;
      closest_pt_idx = i;
    }
  }

  // each path-point spaced 25 cm apart, doing 400 pts gives us total coverage of
  // 10000 cm or 100 meters
  size_t horizon_dist = 400;
  SignalType current_signal = SignalType::None;
  for (size_t offset = 0; offset < horizon_dist; offset++)
  {
    size_t i = (offset + closest_pt_idx) % current_path->points.size();
    // get road that current path point is in
    double x = current_path->points[i].x;
    double y = current_path->points[i].y;
    auto lane = navigator::opendrive::get_lane_from_xy(map, x, y);
    if (lane == nullptr)
    {
      // RCLCPP_INFO(this->get_logger(), "(%f, %f): no road found for behavior planner", this->current_position_x, this->current_position_y);
      continue;
    }

    // check if road is a junction
    auto junction = lane->road.lock()->junction;
    auto id = lane->road.lock()->id;
    if (current_signal != SignalType::Stop)
    {
      // stop is the most restrictive, so if we aren't already under the effect of it, keep looking
      // get all signals on this road
      auto signals = map_info->signals.find(id);
      if (signals != map_info->signals.end())
      {
        double s = lane->road.lock()->ref_line->match(x, y);
        for (const Signal &signal : signals->second)
        {
          // check if signal applies to this point
          if (navigator::opendrive::signal_applies(signal, s, id, lane->id))
          {
            auto new_sig = classify_signal(signal);
            if (new_sig > current_signal)
            {
              // overwrite current signal because the new one is more restrictive
              current_signal = new_sig;
            }
          }
        }
      }
    }
    if (current_signal != SignalType::None && junction != "-1" && seen_junctions.find(junction) == seen_junctions.end())
    {
      // we are under the effect of a signal and in a junction that we haven't seen before
      // make a zone for this junction:
      seen_junctions.insert(junction);
      junction_ids.push_back(junction);
      zones_made = true;
      Zone zone = navigator::zones_lib::to_zone_msg(map->junctions[junction], map);
      switch (current_signal)
      {
      case SignalType::Yield:
        zone.max_speed = YIELD_SPEED;
        break;
      case SignalType::Stop:
        zone.max_speed = STOP_SPEED;
        break;
      default:
        zone.max_speed = 0;
        RCLCPP_WARN(this->get_logger(), "unknown signal type %d", (int)current_signal);
        break;
      }
      final_zones.zones.push_back(zone);
    }
  }

  return zones_made;
}

BehaviorPlannerNode::SignalType BehaviorPlannerNode::classify_signal(const navigator::opendrive::Signal &signal)
{
  if (signal.type == "206")
  {
    return SignalType::Stop;
  }
  if (signal.type == "205")
  {
    return SignalType::Yield;
  }
  return SignalType::None;
}