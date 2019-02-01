/* Copyright (c) 2017, United States Government, as represented by the
 * Administrator of the National Aeronautics and Space Administration.
 *
 * All rights reserved.
 *
 * The Astrobee platform is licensed under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with the
 * License. You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */

// Libsurvive interface
extern "C" {
  #include <deepdive/deepdive.h>
}

// Standard C includes
#include <stdio.h>
#include <stdlib.h>

// ROS includes
#include <ros/ros.h>
#include <nodelet/nodelet.h>
#include <tf2_ros/transform_broadcaster.h>

// Standard C++ includes
#include <iostream>

// Hive services
#include <hive/ViveCalibrationTrackerArray.h>
#include <hive/ViveCalibrationLighthouseArray.h>
#include <hive/ViveCalibrationGeneral.h>
#include <hive/ViveLight.h>
#include <sensor_msgs/Imu.h>
#include <geometry_msgs/Vector3.h>

// Hive
#include <hive/vive_general.h>

// C++ includes
#include <thread>
#include <functional>

static ros::Publisher pub_light_;               // Light publisher
static ros::Publisher pub_imu_;                 // Imu publisher
static ros::Publisher pub_lighthouses_;         // Lighthouse calibration
static ros::Publisher pub_trackers_;            // Tracker calibration
static ros::Publisher pub_general_;             // General calibration

geometry_msgs::Vector3 array_to_ros_vector(float* array) {
  geometry_msgs::Vector3 v;
  v.x = array[0];
  v.y = array[1];
  v.z = array[2];
  return v;
}

class HiveBridge {
 public:
  HiveBridge() {
    active_ = true;
  }

  // Destructor
  ~HiveBridge() {
    active_ = false;
    thread_.join();
  }

  void Initialize(ros::NodeHandle *nh) {
    // Create data publishers
    pub_light_ = nh->advertise<hive::ViveLight>(
      TOPIC_HIVE_LIGHT, 1000);
    pub_imu_ = nh->advertise<sensor_msgs::Imu>(
      TOPIC_HIVE_IMU, 1000);

    // Create calibration publishers as latched
    pub_lighthouses_ = nh->advertise<hive::ViveCalibrationLighthouseArray>(
      TOPIC_HIVE_LIGHTHOUSES, 1000, true);
    pub_trackers_ = nh->advertise<hive::ViveCalibrationTrackerArray>(
      TOPIC_HIVE_TRACKERS, 1000, true);
    pub_general_ = nh->advertise<hive::ViveCalibrationGeneral>(
      TOPIC_HIVE_GENERAL, 1000, true);

    // Start a thread to listen to vive
    thread_ = std::thread(&HiveBridge::WorkerThread, this);
  }

  // Worker thread polls on survive library
  void WorkerThread() {
    // Try to initialize vive
    driver_ = deepdive_init();
    if (!driver_) {
      ROS_FATAL("Vive context init failed");
      return;
    }
    // Set active to true on initialization
    active_ = true;
    // Install the light callback
    deepdive_install_light_fn(driver_, LightCallback);
    deepdive_install_imu_fn(driver_, ImuCallback);
    deepdive_install_lighthouse_fn(driver_, LighthouseCallback);
    deepdive_install_tracker_fn(driver_, TrackerCallback);
    // deepdive_install_general_fn(driver_, GeneralCallback);
    // Keep polling until we are no longer active
    while (active_ && (deepdive_poll(driver_) == 0)) {}
    // Close the vive context
    deepdive_close(driver_);
  }


  // Callback to display light info
  static void LightCallback(struct Tracker * tracker,
    struct Lighthouse * lighthouse, uint8_t axis, uint32_t synctime,
    uint16_t num_sensors, uint16_t *sensors, uint32_t *sweeptimes,
    uint32_t *angles, uint16_t *lengths) {
    static hive::ViveLight msg;
    msg.header.frame_id = tracker->serial;
    msg.header.stamp = ros::Time::now();
    msg.lighthouse = lighthouse->serial;
    msg.axis = axis;
    msg.samples.resize(num_sensors);
    for (uint16_t i = 0; i < num_sensors; i++) {
      msg.samples[i].sensor = sensors[i];
      msg.samples[i].angle =
        (M_PI / 400000.0) * (static_cast<float>(angles[i]) - 200000.0);
      msg.samples[i].length =
        static_cast<float>(lengths[i]) / 48000000.0 * 1000000.0;
    }
    // Publish the data
    pub_light_.publish(msg);
  }

  // Called back when new IMU data is available
  static void ImuCallback(struct Tracker * tracker, uint32_t timecode,
  int16_t acc[3], int16_t gyr[3], int16_t mag[3]) {
    // Package up the IMU data
    static sensor_msgs::Imu msg;
    msg.header.frame_id = tracker->serial;
    msg.header.stamp = ros::Time::now();
    msg.linear_acceleration.x =
      static_cast<float>(acc[0]) * GRAVITY / ACC_SCALE;
    msg.linear_acceleration.y =
      static_cast<float>(acc[1]) * GRAVITY / ACC_SCALE;
    msg.linear_acceleration.z =
      static_cast<float>(acc[2]) * GRAVITY / ACC_SCALE;
    msg.angular_velocity.x =
      static_cast<float>(gyr[0]) * (1./GYRO_SCALE) * (M_PI/180.);
    msg.angular_velocity.y =
      static_cast<float>(gyr[1]) * (1./GYRO_SCALE) * (M_PI/180.);
    msg.angular_velocity.z =
      static_cast<float>(gyr[2]) * (1./GYRO_SCALE) * (M_PI/180.);
    // Publish the data
    pub_imu_.publish(msg);
  }

  // Configuration call from the vive_tool
  static void TrackerCallback(struct Tracker * t) {
    if (!t) return;
    static hive::ViveCalibrationTrackerArray msg;
    // Check if the serial number exists, and if not, create a new record
    std::vector<hive::ViveCalibrationTracker>::iterator it;
    for (it = msg.trackers.begin(); it != msg.trackers.end(); it++)
      if (!it->serial.compare(t->serial)) break;
    if (it == msg.trackers.end())
      it = msg.trackers.insert(msg.trackers.end(),
        hive::ViveCalibrationTracker());
    // Now modify the record
    it->serial = t->serial;
    it->timestamp = ros::Time::now();
    it->extrinsics.resize(t->cal.num_channels);
    for (size_t i = 0; i < t->cal.num_channels; i++) {
      it->extrinsics[i].id = t->cal.channels[i];
      it->extrinsics[i].position =
        array_to_ros_vector(t->cal.positions[i]);
      it->extrinsics[i].normal =
        array_to_ros_vector(t->cal.normals[i]);
    }
    it->acc_bias = array_to_ros_vector(t->cal.acc_bias);
    it->acc_scale = array_to_ros_vector(t->cal.acc_scale);
    it->gyr_bias = array_to_ros_vector(t->cal.gyr_bias);
    it->gyr_scale = array_to_ros_vector(t->cal.gyr_scale);
    // Republish the complete array with the new record
    pub_trackers_.publish(msg);
  }

  // Configuration call from the vive_tool
  static void LighthouseCallback(struct Lighthouse *l) {
    if (!l) return;
    static hive::ViveCalibrationLighthouseArray msg;
    // Check if the serial number exists, and if not, create a new record
    std::vector<hive::ViveCalibrationLighthouse>::iterator it;
    for (it = msg.lighthouses.begin(); it != msg.lighthouses.end(); it++)
      if (!it->serial.compare(l->serial)) break;
    if (it == msg.lighthouses.end())
      it = msg.lighthouses.insert(msg.lighthouses.end(),
        hive::ViveCalibrationLighthouse());
    // Now modify the record
    it->serial = l->serial;
    it->id = l->id;
    it->timestamp = ros::Time::now();
    it->vertical.phase        = l->motors[MOTOR_AXIS_0].phase;
    it->vertical.tilt         = l->motors[MOTOR_AXIS_0].tilt;
    it->vertical.gibphase     = l->motors[MOTOR_AXIS_0].gibphase;
    it->vertical.gibmag       = l->motors[MOTOR_AXIS_0].gibmag;
    it->vertical.curve        = l->motors[MOTOR_AXIS_0].curve;
    it->horizontal.phase      = l->motors[MOTOR_AXIS_1].phase;
    it->horizontal.tilt       = l->motors[MOTOR_AXIS_1].tilt;
    it->horizontal.gibphase   = l->motors[MOTOR_AXIS_1].gibphase;
    it->horizontal.gibmag     = l->motors[MOTOR_AXIS_1].gibmag;
    it->horizontal.curve      = l->motors[MOTOR_AXIS_1].curve;
    // Republish the complete array with the new record
    pub_lighthouses_.publish(msg);
  }

  // Query for general vive data and convert to a ROS message
  // static void GeneralCallback(struct General *g) {
  //   if (!g) return;
  //   static hive::ViveCalibrationGeneral msg;
  //   msg.timebase_hz            = g->timebase_hz;
  //   msg.timecenter_ticks       = g->timecenter_ticks;
  //   msg.pulsedist_max_ticks    = g->pulsedist_max_ticks;
  //   msg.pulselength_min_sync   = g->pulselength_min_sync;
  //   msg.pulse_in_clear_time    = g->pulse_in_clear_time;
  //   msg.pulse_max_for_sweep    = g->pulse_max_for_sweep;
  //   msg.pulse_synctime_offset  = g->pulse_synctime_offset;
  //   msg.pulse_synctime_slack   = g->pulse_synctime_slack;
  //   // Republish
  //   pub_general_.publish(msg);
  // }

 protected:
  struct Driver *driver_;                     // Vive interface
  std::thread thread_;                        // Thread
  bool active_;                               // Active

 private:
  static constexpr double GRAVITY = 9.80665;
  static constexpr double GYRO_SCALE = 32.768;
  static constexpr double ACC_SCALE = 4096.0;
  static constexpr int MOTOR_AXIS_0 = 0;
  static constexpr int MOTOR_AXIS_1 = 1;
};


int main(int argc, char ** argv) {
  ros::init(argc, argv, "bridge");
  ros::NodeHandle nh;
  HiveBridge bridge;

  bridge.Initialize(&nh);

  ros::spin();

  return 0;
}
