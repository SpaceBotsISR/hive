/**
 *
 *
 *
 *
*/

#include <hive/vive_base.h>

namespace base{
  double start_pose[6] = {0, 0, 1, 0, 0, 0};
}

BaseSolve::BaseSolve() {
  // do nothing
}

BaseSolve::BaseSolve(Environment environment,
  Tracker tracker) {
  for (std::map<uint8_t, Sensor>::const_iterator sn_it = tracker.sensors.begin();
    sn_it != tracker.sensors.end(); sn_it++) {
    if (unsigned(sn_it->first) >= TRACKER_SENSORS_NUMBER
      || unsigned(sn_it->first) < 0) {
      ROS_FATAL("Sensor ID is invalid.");
      return;
    }
    extrinsics_.positions[3 * unsigned(sn_it->first)]     = sn_it->second.position.x;
    extrinsics_.positions[3 * unsigned(sn_it->first) + 1] = sn_it->second.position.y;
    extrinsics_.positions[3 * unsigned(sn_it->first) + 2] = sn_it->second.position.z;
    extrinsics_.normals[3 * unsigned(sn_it->first)]       = sn_it->second.normal.x;
    extrinsics_.normals[3 * unsigned(sn_it->first) + 1]   = sn_it->second.normal.y;
    extrinsics_.normals[3 * unsigned(sn_it->first) + 2]   = sn_it->second.normal.z;
  }
  extrinsics_.size = tracker.sensors.size();
  extrinsics_.radius = 0.005;
  tracker_ = tracker;
  environment_ = environment;
  for (size_t i = 0; i < 6; i++) tracker_pose_.transform[i] = base::start_pose[i];
}

BaseSolve::~BaseSolve() {
  // Do nothing
}

bool BaseSolve::GetTransform(geometry_msgs::TransformStamped &msg) {
  // Filling the translation data
  if (!tracker_pose_.valid) {
    return false;
  }
  msg.transform.translation.x = tracker_pose_.transform[0];
  msg.transform.translation.y = tracker_pose_.transform[1];
  msg.transform.translation.z = tracker_pose_.transform[2];
  // Axis Angle convertion to quaternion
  Eigen::Vector3d axis_angle_vec = Eigen::Vector3d(
    tracker_pose_.transform[3],
    tracker_pose_.transform[4],
    tracker_pose_.transform[5]);
  double angle = axis_angle_vec.norm();
  axis_angle_vec.normalize();
  Eigen::AngleAxisd axis_angle = Eigen::AngleAxisd(angle, axis_angle_vec);
  Eigen::Quaterniond quaternion_vec = Eigen::Quaterniond(axis_angle);
  // Filling the orietation data
  msg.transform.rotation.x = quaternion_vec.x();
  msg.transform.rotation.y = quaternion_vec.y();
  msg.transform.rotation.z = quaternion_vec.z();
  msg.transform.rotation.w = quaternion_vec.w();
  // Setting the frames
  msg.child_frame_id = tracker_.serial;
  msg.header.frame_id = environment_.vive.child_frame;
  msg.header.stamp = tracker_pose_.stamp;
  // Prevent repeated use of the same pose
  tracker_pose_.valid = false;
  return true;
}

struct BundleHorizontalAngle{
  explicit BundleHorizontalAngle(LightVec horizontal_observations) :
  horizontal_observations_(horizontal_observations) {}

  template <typename T> bool operator()(const T* const * parameters, T * residual) const {
    // Rotation matrix from the tracker's frame to the vive frame
    Eigen::Matrix<T, 3, 3> vRt;
    ceres::AngleAxisToRotationMatrix(&parameters[POSE][3], vRt.data());
    // Position of the tracker in the vive frame
    Eigen::Matrix<T, 3, 1> vPt;
    vPt << parameters[POSE][0], parameters[POSE][1], parameters[POSE][2];
    // Position of the sensor in the tracker's frame
    Eigen::Matrix<T, 3, 1> tPs;
    // Orientation of the lighthouse in the vive frame
    Eigen::Matrix<T, 3, 3> vRl;
    ceres::AngleAxisToRotationMatrix(&parameters[LIGHTHOUSE][3], vRl.data());
    Eigen::Matrix<T, 3, 3> lRv = vRl.transpose();
    // Position of the lighthouse in the vive frame
    Eigen::Matrix<T, 3, 1> vPl;
    vPl << parameters[LIGHTHOUSE][0], parameters[LIGHTHOUSE][1], parameters[LIGHTHOUSE][2];
    Eigen::Matrix<T, 3, 1> lPv = - lRv * vPl;

    // Create residuals
    for (size_t i = 0; i < horizontal_observations_.size(); i++) {
      // Position of the photodiode
      tPs << parameters[EXTRINSICS][3*horizontal_observations_[i].sensor_id + 0],
      parameters[EXTRINSICS][3*horizontal_observations_[i].sensor_id + 1],
      parameters[EXTRINSICS][3*horizontal_observations_[i].sensor_id + 2];
      Eigen::Matrix<T, 3, 1> lPs = lRv * vRt * tPs + (lRv * vPt + lPv);

      T ang; // The final angle
      T x = (lPs(0)/lPs(2)); // Vertical angle
      ang = atan(x);

      residual[i] = T(horizontal_observations_[i].angle) - ang;
    }

    return true;
  }

 private:
  LightVec horizontal_observations_;
};

struct BundleVerticalAngle{
  explicit BundleVerticalAngle(LightVec vertical_observations) :
  vertical_observations_(vertical_observations) {}

  template <typename T> bool operator()(const T* const * parameters, T * residual) const {
    // Rotation matrix from the tracker's frame to the vive frame
    Eigen::Matrix<T, 3, 3> vRt;
    ceres::AngleAxisToRotationMatrix(&parameters[POSE][3], vRt.data());
    // Position of the tracker in the vive frame
    Eigen::Matrix<T, 3, 1> vPt;
    vPt << parameters[POSE][0], parameters[POSE][1], parameters[POSE][2];
    // Position of the sensor in the tracker's frame
    Eigen::Matrix<T, 3, 1> tPs;
    // Orientation of the lighthouse in the vive frame
    Eigen::Matrix<T, 3, 3> vRl;
    ceres::AngleAxisToRotationMatrix(&parameters[LIGHTHOUSE][3], vRl.data());
    Eigen::Matrix<T, 3, 3> lRv = vRl.transpose();
    // Position of the lighthouse in the vive frame
    Eigen::Matrix<T, 3, 1> vPl;
    vPl << parameters[LIGHTHOUSE][0], parameters[LIGHTHOUSE][1], parameters[LIGHTHOUSE][2];
    Eigen::Matrix<T, 3, 1> lPv = - lRv * vPl;

    // Create residuals
    for (size_t i = 0; i < vertical_observations_.size(); i++) {
      // Position of the photodiode
      tPs << parameters[EXTRINSICS][3*vertical_observations_[i].sensor_id + 0],
      parameters[EXTRINSICS][3*vertical_observations_[i].sensor_id + 1],
      parameters[EXTRINSICS][3*vertical_observations_[i].sensor_id + 2];
      Eigen::Matrix<T, 3, 1> lPs = lRv * vRt * tPs + (lRv * vPt + lPv);

      T ang; // The final angle
      T y = (lPs(1)/lPs(2)); // Vertical angle
      ang = atan(y);

      residual[i] = T(vertical_observations_[i].angle) - ang;
    }

    return true;
  }

 private:
  LightVec vertical_observations_;
};

bool ComputeTransformBundle(LightData observations,
  SolvedPose * pose_tracker,
  Extrinsics * extrinsics,
  Environment * environment) {

  ceres::Problem problem;
  double pose[6];// = {0.017356, -0.00947887, 1.53151, -0.799895, 2.22509, -0.436057};
  std::map<std::string, double[6]> lighthouses_pose;

  for (int i = 0; i < 6; i++) {
    pose[i] = pose_tracker->transform[i];
  }

  size_t n_sensors = 0, counter = 0;
  for (LightData::iterator ld_it = observations.begin();
    ld_it != observations.end(); ld_it++) {
    // Get lighthouse to angle axis
    bool lighthouse = false;
    if (environment->lighthouses.find(ld_it->first) == environment->lighthouses.end())
      continue;

    Eigen::AngleAxisd tmp_AA = Eigen::AngleAxisd(
      Eigen::Quaterniond(
        environment->lighthouses[ld_it->first].rotation.w,
        environment->lighthouses[ld_it->first].rotation.x,
        environment->lighthouses[ld_it->first].rotation.y,
        environment->lighthouses[ld_it->first].rotation.z).toRotationMatrix());
    lighthouses_pose[ld_it->first][0] = environment->lighthouses[ld_it->first].translation.x;
    lighthouses_pose[ld_it->first][1] = environment->lighthouses[ld_it->first].translation.y;
    lighthouses_pose[ld_it->first][2] = environment->lighthouses[ld_it->first].translation.z;
    lighthouses_pose[ld_it->first][3] = tmp_AA.axis()(0) * tmp_AA.angle();
    lighthouses_pose[ld_it->first][4] = tmp_AA.axis()(1) * tmp_AA.angle();
    lighthouses_pose[ld_it->first][5] = tmp_AA.axis()(2) * tmp_AA.angle();
    // Filling the solve with the data
    if (ld_it->second.axis[HORIZONTAL].lights.size() != 0) {
      n_sensors += ld_it->second.axis[HORIZONTAL].lights.size();
      ceres::DynamicAutoDiffCostFunction<BundleHorizontalAngle, 4> * horizontal_cost =
      new ceres::DynamicAutoDiffCostFunction<BundleHorizontalAngle, 4>
      (new BundleHorizontalAngle(ld_it->second.axis[HORIZONTAL].lights));
      horizontal_cost->AddParameterBlock(6);
      horizontal_cost->AddParameterBlock(3 * extrinsics->size);
      horizontal_cost->AddParameterBlock(6);
      horizontal_cost->AddParameterBlock(1);
      horizontal_cost->SetNumResiduals(ld_it->second.axis[HORIZONTAL].lights.size());

      problem.AddResidualBlock(horizontal_cost,
        NULL,
        pose,
        extrinsics->positions,
        lighthouses_pose[ld_it->first],
        &(extrinsics->radius));

      lighthouse = lighthouse || true;
    }
    if (ld_it->second.axis[VERTICAL].lights.size() != 0) {
      n_sensors += ld_it->second.axis[VERTICAL].lights.size();
      ceres::DynamicAutoDiffCostFunction<BundleVerticalAngle, 4> * vertical_cost =
      new ceres::DynamicAutoDiffCostFunction<BundleVerticalAngle, 4>
      (new BundleVerticalAngle(ld_it->second.axis[VERTICAL].lights));
      vertical_cost->AddParameterBlock(6);
      vertical_cost->AddParameterBlock(3 * extrinsics->size);
      vertical_cost->AddParameterBlock(6);
      vertical_cost->AddParameterBlock(1);
      vertical_cost->SetNumResiduals(ld_it->second.axis[VERTICAL].lights.size());

      problem.AddResidualBlock(vertical_cost,
        NULL,
        pose,
        extrinsics->positions,
        lighthouses_pose[ld_it->first],
        &(extrinsics->radius));

      lighthouse = lighthouse || true;
    }
    if (lighthouse) {
      problem.SetParameterBlockConstant(lighthouses_pose[ld_it->first]);
      counter++;
    }
  }
  // Exit in case no lighthouses are available
  if (counter == 0) {
    return false;
  }
  problem.SetParameterBlockConstant(extrinsics->positions);
  problem.SetParameterBlockConstant(&(extrinsics->radius));

  ceres::Solver::Options options;
  ceres::Solver::Summary summary;

  options.minimizer_progress_to_stdout = false;
  options.linear_solver_type = ceres::DENSE_SCHUR;
  options.max_solver_time_in_seconds = 1;

  ceres::Solve(options, &problem, &summary);

  std::cout << std::setprecision(4) << "CTB: " 
    << std::setprecision(4) << summary.final_cost << " - "
    << std::setprecision(4) << pose[0] << ", "
    << std::setprecision(4) << pose[1] << ", "
    << std::setprecision(4) << pose[2] << ", "
    << std::setprecision(4) << pose[3] << ", "
    << std::setprecision(4) << pose[4] << ", "
    << std::setprecision(4) << pose[5];

  // Check if valid
  double pose_norm = sqrt(pose[0]*pose[0] + pose[1]*pose[1] + pose[2]*pose[2]);
  if (summary.final_cost > 1e-4 * static_cast<double>(unsigned(n_sensors))
    || pose_norm > 20
    || pose[2] <= 0 ) {
    std::cout << " - INVALID" << std::endl;
    // std::cout << " x ";
    return false;
  }
  std::cout << " - VALID" << std::endl;
  // std::cout << " . ";

  double angle_norm = sqrt(pose[3]*pose[3] + pose[4]*pose[4] + pose[5]*pose[5]);
  // Change the axis angle to an acceptable interval
  while (angle_norm > M_PI) {
    pose[3] = pose[3] / angle_norm * (angle_norm - 2 * M_PI);
    pose[4] = pose[4] / angle_norm * (angle_norm - 2 * M_PI);
    pose[5] = pose[5] / angle_norm * (angle_norm - 2 * M_PI);
    angle_norm = sqrt(pose[3]*pose[3] + pose[4]*pose[4] + pose[5]*pose[5]);
  }
  // Change the axis angle to an acceptable interval
  while (angle_norm < - M_PI) {
    pose[3] = pose[3] / angle_norm * (angle_norm + 2 * M_PI);
    pose[4] = pose[4] / angle_norm * (angle_norm + 2 * M_PI);
    pose[5] = pose[5] / angle_norm * (angle_norm + 2 * M_PI);
    angle_norm = sqrt(pose[3]*pose[3] + pose[4]*pose[4] + pose[5]*pose[5]);
  }

  // Save the solved pose
  for (int i = 0; i < 6; i++) {
    pose_tracker->transform[i] = pose[i];
  }
  pose_tracker->valid = true;
  // pose_tracker->stamp = ros::Time::now();

  return true;
}

void BaseSolve::ProcessLight(const hive::ViveLight::ConstPtr& msg) {
  if (msg == NULL) {
    return;
  }

  // Check if this is a new lighthouse
  if (poses_.find(msg->lighthouse) == poses_.end()) {
    // Set structures
    observations_[msg->lighthouse].lighthouse = msg->lighthouse;
  }

  // Clear the axis with old data - should only do this if the new data is good
  observations_[msg->lighthouse].axis[msg->axis].lights.clear();

  // Iterate all sweep data
  for (std::vector<hive::ViveLightSample>::const_iterator li_it = msg->samples.begin();
    li_it != msg->samples.end(); li_it++) {
    // Detect non-sense data
    if (li_it->sensor == -1
      || li_it->angle > M_PI/3
      || li_it->angle < -M_PI/3) {
      continue;
    }

    // New Light
    Light light;
    light.sensor_id = li_it->sensor;
    light.angle = li_it->angle;
    light.timecode = li_it->timecode;
    light.length = li_it->length;

    // Add data to the axis
    observations_[msg->lighthouse].axis[msg->axis].lights.push_back(light);
    observations_[msg->lighthouse].axis[msg->axis].stamp = msg->header.stamp;
  }

  // Remove old data
  for (LightData::iterator lh_it = observations_.begin();
    lh_it != observations_.end(); lh_it++) {
    for (std::map<uint8_t, LightVecStamped>::iterator ax_it = lh_it->second.axis.begin();
      ax_it != lh_it->second.axis.end(); ax_it++) {
      ros::Duration elapsed = ax_it->second.stamp - msg->header.stamp;
      if (elapsed.toNSec() >= 50e6)
        ax_it->second.lights.clear();
    }
  }

  if (observations_[msg->lighthouse].axis[HORIZONTAL].lights.size() > 3
    && observations_[msg->lighthouse].axis[VERTICAL].lights.size() > 3) {
    // Set the timestamp to be the sames as the measurement
    tracker_pose_.stamp = msg->header.stamp;
    ComputeTransformBundle(observations_,
      &tracker_pose_,
      &extrinsics_,
      &environment_);
  }
  return;
}

void BaseSolve::ProcessImu(const sensor_msgs::Imu::ConstPtr& msg) {
  // Do nothing
  return;
}