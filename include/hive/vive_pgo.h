#ifndef HIVE_VIVE_PGO
#define HIVE_VIVE_PGO

// ROS includes
#include <ros/ros.h>
#include <rosbag/bag.h>
#include <rosbag/view.h>

// Hive includes
#include <hive/vive_general.h>
#include <hive/vive_solver.h>
#include <hive/vive_solve.h>
// #include <hive/vive_cost.h>
#include <hive/vive.h>

// Hive msgs
#include <hive/ViveLight.h>
#include <sensor_msgs/Imu.h>
#include <hive/ViveCalibration.h>

// Ceres and logging
#include <ceres/ceres.h>
#include <ceres/rotation.h>

// Eigen includes
#include <Eigen/Dense>
#include <Eigen/Geometry>
#include <unsupported/Eigen/MatrixFunctions>

// Eigen C++ includes
#include <vector>
#include <map>
#include <string>

#define SMOOTHING 1e-1
#define ROTATION_FACTOR 1.0

class PoseGraph : public Solver {
public:
  // Constructor
  PoseGraph();
  PoseGraph(Environment environment,
  Tracker tracker,
  std::map<std::string, Lighthouse> lighthouses,
  size_t window,
  double trust,
  double first_factor,
  bool correction);
  // Destructor
  ~PoseGraph();
  // New light data
  void ProcessLight(const hive::ViveLight::ConstPtr& msg);
  // New Imu data
  void ProcessImu(const sensor_msgs::Imu::ConstPtr& msg);
  // Get the tracker's pose
  bool GetTransform(geometry_msgs::TransformStamped& msg);
  // Prinst stuff
  void PrintState();
private:
  // Aux method
  void RemoveLight();
  // Aux method
  void RemoveImu();
  // Add new pose to the back
  void AddPoseBack();
  // Add new pose to the front
  void AddPoseFront();
  // Limit the vector of poses
  void ApplyLimits();
  // Solve the problem
  bool Solve();
  // Validity of the pose
  bool Valid();
  
private:
  // The pose (light pose in vive frame)
  geometry_msgs::TransformStamped pose_;
  // Light data
  std::vector<hive::ViveLight> light_data_;
  // Inertial data
  std::vector<sensor_msgs::Imu> imu_data_;
  // Calibrated environment
  Environment environment_;
  // Tracker
  Tracker tracker_;
  // Lighthouses specs
  std::map<std::string, Lighthouse> lighthouses_;
  // Correction
  bool correction_;
  // Force the first the first pose to be close to its previous estimate.
  // bool force_first_;
  // Optimization window
  size_t window_;
  /*Trust on the inertial measurements
    -> Too high - the noice may take over
    -> Too low - consecutive poses will pushed apart
  */
  double trust_;
  //
  // double smoothing_ = SMOOTHING;
  double first_factor_;
  // Validity
  bool valid_;
  // Internal
  std::vector<double*> poses_;
  bool lastposewasimu_;
  double last_cost_;
};

#endif // HIVE_VIVE_PGO