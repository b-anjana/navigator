# Copyright 2016 Open Source Robotics Foundation, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""
    Subscribe to our NDT pose and republish as
    a VehicleKinematicState. Simple!
"""

import rclpy
from rclpy.node import Node

from std_msgs.msg import String, Header
from autoware_auto_msgs.msg import VehicleKinematicState
from autoware_auto_msgs.msg import Trajectory
from autoware_auto_msgs.msg import TrajectoryPoint
from geometry_msgs.msg import PoseStamped, Point, PoseWithCovarianceStamped
from nav_msgs.msg import Path
from visualization_msgs.msg import Marker
import math

class VehicleBridge(Node):

    def __init__(self):
        super().__init__('translator')
        self.pose_sub = self.create_subscription(
            PoseWithCovarianceStamped,
            '/localization/ndt_pose',
            self.pose_cb,
            10)

        self.kinematic_state_pub = self.create_publisher(
            VehicleKinematicState,
            '/vehicle/vehicle_kinematic_state',
            10)
        # self.path_pub_ = self.create_publisher(Path, '/planning/path_viz', 10)
        # self.command_viz_pub_ = self.create_publisher(Marker, '/vehicle/vehicle_command_viz', 10)
        # self.hack_header = Header()

        # self.subscription  # prevent unused variable warning

    def pose_cb(self, msg):
        position = msg.pose.pose.position
        self.get_logger().debug("Pose: {},{},{}".format(position.x, position.y, position.z))

        # Simply form and publish a VehicleKinematicState from NDT Pose
        kstate = VehicleKinematicState()
        kstate.header = msg.header
        kstate.state.x = position.x
        kstate.state.y = position.y

        self.kinematic_state_pub.publish(kstate)

        # printstring = "\n\n";
        # printstring+= "Turn: {}\n".format(msg.front_wheel_angle_rad)
        # printstring+= "Accel: {}\n".format(msg.long_accel_mps2)
        # self.get_logger().info(printstring)
        # commandMarker = Marker()
        # commandMarker.header = self.hack_header
        # commandMarker.header.frame_id = "base_link"
        # commandMarker.type = Marker.ARROW
        # commandMarker.ns = 'vehicle_command_viz'
        # commandMarker.id = 0
        # commandMarker.action = Marker.ADD

        # markerStart = Point()
        # markerStart.x = 0.0
        # markerStart.y = 0.0
        # markerStart.z = 0.0
        # markerEnd = Point()
        # markerEnd.y = 5*msg.front_wheel_angle_rad # math.asin(msg.front_wheel_angle_rad)
        # markerEnd.x = 0.0 #math.acos(msg.front_wheel_angle_rad)
        # markerEnd.z = 0.0
        # # commandMarker.pose.position.x = 0.0
        # # commandMarker.pose.position.y = 0.0
        # # commandMarker.pose.position.z = 0.0
        # # commandMarker.pose.orientation.x = 0.0
        # # commandMarker.pose.orientation.y = 0.0
        # # commandMarker.pose.orientation.z = 0.0
        # # commandMarker.pose.orientation.w = 1.0
        # commandMarker.scale.x = 0.5
        # commandMarker.scale.y = 1.0
        # commandMarker.scale.z = 1.0
        # commandMarker.color.a = 1.0
        # commandMarker.color.r = 0.0
        # commandMarker.color.g = 1.0
        # commandMarker.color.b = 0.0
        # commandMarker.points = [markerStart, markerEnd]
        # self.command_viz_pub_.publish(commandMarker)
    
    # def trajectory_cb(self, msg):
    #     print(len(msg.points))
    #     path = Path()
    #     path.header = msg.header
    #     poseArray = []
    #     for trajPoint in msg.points:
    #         pose = PoseStamped()
    #         pose.pose.position.x = trajPoint.x
    #         pose.pose.position.y = trajPoint.y
    #         poseArray.append(pose)
    #     path.poses = poseArray
    #     self.path_pub_.publish(path)
    #     self.hack_header = msg.header


def main(args=None):
    rclpy.init(args=args)

    bridge = VehicleBridge()

    rclpy.spin(bridge)

    # Destroy the node explicitly
    # (optional - otherwise it will be done automatically
    # when the garbage collector destroys the node object)
    bridge.destroy_node()
    rclpy.shutdown()


if __name__ == '__main__':
    main()
