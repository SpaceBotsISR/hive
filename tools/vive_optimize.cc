// C standard includes
#include <stdlib.h>

// C++ standard includes
#include <iostream>
#include <mutex>

// Ceres and logging
#include <ceres/ceres.h>

// Eigen C++ includes
#include <Eigen/Dense>
#include <Eigen/Geometry>

// ROS includes
#include <ros/ros.h>
#include <rosbag/bag.h>
#include <rosbag/view.h>

#include <std_msgs/Int32.h>
#include <std_msgs/String.h>
// ROS messages
#include <hive/ViveLight.h>
#include <hive/HiveData.h>

// Hive includes
#include "hive/vive_solve.h"
#include "hive/vive.h"

template <typename T> T max4(T a, T b, T c, T d) {
  T max = a;
  if (b > max) max = b;
  if (c > max) max = c;
  if (d > max) max = d;
  return max;
}

template <typename T> T min4(T a, T b, T c, T d) {
  T min = a;
  if (b < min) min = b;
  if (c < min) min = c;
  if (d < min) min = d;
  return min;
}

// Ceres Solver Cost Functors
struct Model{
  explicit Model(double * lTt, double * tPs, double * tNs, double obs, uint8_t axis) :
    lTt_(lTt), tPs_(tPs), tNs_(tNs), obs_(obs), axis_(axis) {}

  // parameters includes: angle of the sensor and its size
  template <typename T> bool operator()(const T* const * parameters, T * residual) const {
    // parameters[0] is sensor size
    // parameters[1] is sensor rotation
    // Tracker pose
    // std::cout << "lTt - "
    //   << lTt_[0] << ", "
    //   << lTt_[1] << ", "
    //   << lTt_[2] << ", "
    //   << lTt_[3] << ", "
    //   << lTt_[4] << ", "
    //   << lTt_[5] << std::endl;
    // std::cout << "tPs - "
    //   << tPs_[0] << ", "
    //   << tPs_[1] << ", "
    //   << tPs_[2] << std::endl;
    // std::cout << "tNs - "
    //   << tNs_[0] << ", "
    //   << tNs_[1] << ", "
    //   << tNs_[2] << std::endl;
    // std::cout << "obs - " << obs_ << std::endl;
    // std::cout << "axis - " << static_cast<int>(axis_) << std::endl;
    // std::cout << "angle - " << parameters[1][0] << std::endl;
    // std::cout << "size - " << parameters[0][0] << std::endl;
    Eigen::Matrix<T, 3, 1> lPt;
    lPt << T(lTt_[0]), T(lTt_[1]), T(lTt_[2]);
    Eigen::Matrix<T, 3, 1> lAAt;
    lAAt << T(lTt_[3]), T(lTt_[4]), T(lTt_[5]);
    Eigen::Matrix<T, 3, 3> lRt;
    lRt = Eigen::AngleAxis<T>(lAAt.norm(), lAAt / lAAt.norm());
    // Sensor position
    Eigen::Matrix<T, 3, 1> tPs;
    tPs << T(tPs_[0]), T(tPs_[1]), T(tPs_[2]);
    // Sensor normal
    Eigen::Matrix<T, 3, 1> tNs;
    tNs << T(tNs_[0]), T(tNs_[1]), T(tNs_[2]);
    tNs.normalize();
    // Sensor reference
    Eigen::Matrix<T, 3, 1> tOs;
    tOs << T(tNs_[1]), -T(tNs_[0]), T(0.0);
    tOs.normalize();
    // Rotate reference : optimization
    Eigen::Matrix<T, 3, 3> tRs;
    tRs = Eigen::AngleAxis<T>(parameters[1][0], tNs).toRotationMatrix();
    Eigen::Matrix<T, 3, 1> td1 = tRs * tOs;
    td1.normalize();
    Eigen::Matrix<T, 3, 1> td2 = td1.cross(tNs);
    td2.normalize();

    Eigen::Matrix<T, 3, 1> tC1 = (parameters[0][0] / T(2)) * (td1 + td2) + tPs;
    Eigen::Matrix<T, 3, 1> tC2 = (parameters[0][0] / T(2)) * (- td1 - td2) + tPs;
    Eigen::Matrix<T, 3, 1> tC3 = (parameters[0][0] / T(2)) * (- td1 + td2) + tPs;
    Eigen::Matrix<T, 3, 1> tC4 = (parameters[0][0] / T(2)) * (td1 - td2) + tPs;

    Eigen::Matrix<T, 3, 1> lC1 = lRt * tC1 + lPt;
    Eigen::Matrix<T, 3, 1> lC2 = lRt * tC2 + lPt;
    Eigen::Matrix<T, 3, 1> lC3 = lRt * tC3 + lPt;
    Eigen::Matrix<T, 3, 1> lC4 = lRt * tC4 + lPt;

    T alpha1;
    T alpha2;
    T alpha3;
    T alpha4;

    if (axis_ == 0) {
      // Horizontal model
      alpha1 = atan(lC1[0]/lC1[2]);
      alpha2 = atan(lC2[0]/lC2[2]);
      alpha3 = atan(lC3[0]/lC3[2]);
      alpha4 = atan(lC4[0]/lC4[2]);
    } else {
      // Vertical Model
      alpha1 = atan(lC1[1]/lC1[2]);
      alpha2 = atan(lC2[1]/lC2[2]);
      alpha3 = atan(lC3[1]/lC3[2]);
      alpha4 = atan(lC4[1]/lC4[2]);
    }

    Eigen::Matrix<T, 3, 1> lPs = lRt * tPs + lPt;
    // std::cout << lPs(0) << ", " << lPs(1) << ", " << lPs(2) << std::endl;
    // std::cout << lC1(0) << ", " << lC1(1) << ", " << lC1(2) << std::endl
    //   << lC2(0) << ", " << lC2(1) << ", " << lC2(2) << std::endl
    //   << lC3(0) << ", " << lC3(1) << ", " << lC3(2) << std::endl
    //   << lC4(0) << ", " << lC4(1) << ", " << lC4(2) << std::endl;
    // Chose beta
    T beta = max4(alpha1, alpha2, alpha3, alpha4)
      - min4(alpha1, alpha2, alpha3, alpha4);
    beta *= 10e3;

    // std::cout << obs_ << " - " << beta << std::endl;

    residual[0] = (T(obs_) - beta);
    return true;
  }
 private:
  double * lTt_;
  double * tPs_;
  double * tNs_;
  double obs_;
  uint8_t axis_;
};

int main(int argc, char **argv) {

  ros::init(argc, argv, "hive_optimizer");
  ros::NodeHandle nh;

  rosbag::Bag rbag;
  rbag.open("/home/mikebrgs/CurrentWork/thesis/vive1/catkin_ws/src/hive/data/vivebeta4.bag",
    rosbag::bagmode::Read);
  rosbag::View view;

  // Declarations
  std::mutex mtx;
  std::string last_lh;
  LightData observations;
  std::vector<SolvedPose*> pose_vector;
  std::vector<double*> position_vector;
  std::vector<double*> normal_vector;
  SolvedPose ref_pose;
  Lighthouse lh;
  Extrinsics tr;
  Calibration cal;

  std::vector<ceres::DynamicAutoDiffCostFunction<Model, 4>*> cost_vector;

  // Initializations
  ref_pose.transform[0] = 0.0;
  ref_pose.transform[1] = 0.0;
  ref_pose.transform[2] = 1.0;
  ref_pose.transform[3] = 0.0;
  ref_pose.transform[4] = 0.0;
  ref_pose.transform[5] = 0.0;

  uint8_t thesensor = 6;
  double theangle = 5.34614;
  double thesize = 0.00271586;
  // double thesize = 0.0016;
  int thecounter = 0;
  int minsample = 0;
  int maxsample = 10;

  // Trackers
  rosbag::View view_tr(rbag, rosbag::TopicQuery("/loc/vive/trackers"));
  for (auto bag_it = view_tr.begin(); bag_it != view_tr.end(); bag_it++) {
    const hive::ViveCalibrationTrackerArray::ConstPtr vt =
      bag_it->instantiate<hive::ViveCalibrationTrackerArray>();
    cal.SetTrackers(*vt);
  }

  // Lighthouses
  rosbag::View view_lh(rbag, rosbag::TopicQuery("/loc/vive/lighthouses"));
  for (auto bag_it = view_lh.begin(); bag_it != view_lh.end(); bag_it++) {
    const hive::ViveCalibrationLighthouseArray::ConstPtr vl =
      bag_it->instantiate<hive::ViveCalibrationLighthouseArray>();
    cal.SetLighthouses(*vl);
  }

  // Ceres problem
  ceres::Problem problem;
  std::vector<ceres::ResidualBlockId> resblocks;

  // Light data
  rosbag::View view_li(rbag, rosbag::TopicQuery("/loc/vive/light"));
  for (auto bag_it = view_li.begin(); bag_it != view_li.end(); bag_it++) {
    hive::ViveLight::ConstPtr vl = bag_it->instantiate<hive::ViveLight>();
    // std::cout << vl->lighthouse << " - "
      // << static_cast<int>(vl->axis) << std::endl;
    observations[vl->lighthouse].axis[vl->axis].lights.clear();
    for (auto li_it = vl->samples.begin();
      li_it != vl->samples.end(); li_it++) {
      // std::cout << "Sensor " << li_it->sensor << std::endl;
      // Filling in the data structure
      observations[vl->lighthouse].lighthouse = vl->lighthouse;
      observations[vl->lighthouse].axis[vl->axis].stamp = vl->header.stamp;
      Light light;
      light.sensor_id = li_it->sensor;
      light.timecode = li_it->timecode;
      light.angle = li_it->angle;
      light.length = li_it->length;
      observations[vl->lighthouse].axis[vl->axis].lights.push_back(light);
      std::cout << vl->lighthouse << " - ";
      if (vl->axis == HORIZONTAL) {
          std::cout << " H - ";
      } else {
        std::cout << " V - ";
      }
       std::cout << li_it->sensor << " : " << li_it->angle << " - " << li_it->length <<  std::endl;
    }
    // std::cout << std::endl;
    tr.size = ViveUtils::ConvertExtrinsics(cal.trackers[vl->header.frame_id],
      tr.positions);
    // If the data is available
    if (observations[vl->lighthouse].axis[0].lights.size() != 0 &&
      observations[vl->lighthouse].axis[1].lights.size() != 0) {
      // Compute pose of the tracker
      SolvedPose * pose = new SolvedPose();
      for (size_t i = 0; i < 6; i++) pose->transform[i] = ref_pose.transform[i];
      pose_vector.push_back(pose);
      thecounter++;
      if (thecounter <= minsample) {
        continue;
      }
      if (ComputeTransform(observations[vl->lighthouse],
        pose_vector.back(),
        &last_lh,
        &tr,
        &mtx,
        &lh)) {
        ref_pose.transform[0] = pose->transform[0];
        ref_pose.transform[1] = pose->transform[1];
        ref_pose.transform[2] = pose->transform[2];
        ref_pose.transform[3] = pose->transform[3];
        ref_pose.transform[4] = pose->transform[4];
        ref_pose.transform[5] = pose->transform[5];
        // std::cout << "INSIDE" << std::endl;
        // Add cost functor
        double * position = new double[3];
        position[0] = cal.trackers[vl->header.frame_id].sensors[thesensor].position.x;
        position[1] = cal.trackers[vl->header.frame_id].sensors[thesensor].position.y;
        position[2] = cal.trackers[vl->header.frame_id].sensors[thesensor].position.z;
        position_vector.push_back(position);
        double * normal = new double[3];
        normal[0] = cal.trackers[vl->header.frame_id].sensors[thesensor].normal.x;
        normal[1] = cal.trackers[vl->header.frame_id].sensors[thesensor].normal.y;
        normal[2] = cal.trackers[vl->header.frame_id].sensors[thesensor].normal.z;
        normal_vector.push_back(normal);
        // for (auto s_it = cal.trackers[vl->header.frame_id].sensors.begin();
        //   s_it != cal.trackers[vl->header.frame_id].sensors.end(); s_it++) {
        //   std::cout << s_it->second.position.x << ", "
        //   << s_it->second.position.y << ", "
        //   << s_it->second.position.z << std::endl;
        // }
        // std::cout << "tPs_out - "
        //   << position_vector.back()[0] << ", "
        //   << position_vector.back()[1] << ", "
        //   << position_vector.back()[2] << std::endl;
        // std::cout << "tNs_out - "
        //   << normal_vector.back()[0] << ", "
        //   << normal_vector.back()[1] << ", "
        //   << normal_vector.back()[2] << std::endl;

        for (auto li_it = observations[vl->lighthouse].axis[vl->axis].lights.begin();
          li_it != observations[vl->lighthouse].axis[vl->axis].lights.end(); li_it++) {
          // std::cout << "Sensor " << li_it->sensor_id << std::endl;
          if (li_it->sensor_id == thesensor) {
            // std::cout << "YES " << vl->lighthouse << " - " <<
              // static_cast<int>(vl->axis) << " -  " << li_it->sensor_id <<
              // " - " << li_it->angle << std::endl;
            // std::cout << "obs_out - " << li_it->length << std::endl;
            // std::cout << "axis_out - " << static_cast<int>(vl->axis) << std::endl;
            // Model cost
            ceres::DynamicAutoDiffCostFunction<Model, 4> * cost_model =
              new ceres::DynamicAutoDiffCostFunction<Model, 4>(new Model(
                pose_vector.back()->transform,
                position_vector.back(),
                normal_vector.back(),
                li_it->length,
                vl->axis));
            cost_vector.push_back(cost_model);
            cost_model->AddParameterBlock(1);
            cost_model->AddParameterBlock(1);
            cost_model->SetNumResiduals(1);
            ceres::ResidualBlockId res = problem.AddResidualBlock(cost_model, NULL, &thesize, &theangle);
            resblocks.push_back(res);
            // problem.SetParameterBlockConstant(&theangle);

            // ceres::Solver::Options options;
            // ceres::Solver::Summary summary;

            // options.minimizer_progress_to_stdout = false;
            // options.linear_solver_type = ceres::DENSE_SCHUR;
            // // options.max_solver_time_in_seconds = 1.0;
            // // options.minimizer_type = ceres::LINE_SEARCH;

            // ceres::Solve(options, &problem, &summary);

            // std::cout << summary.FullReport() << std::endl;
            // std::cout << "Size: " << thesize << std::endl;
            // std::cout << "Angle: " << theangle << std::endl;
          } else {
            // std::cout << "NOT " << vl->lighthouse << " - "
              // << static_cast<int>(vl->axis) << " - " << li_it->sensor_id
              // << " - " << li_it->angle << std::endl;
          }
        }
      } else {
        thecounter--;
      }
    }

    if (thecounter >   maxsample) {
      // std::cout << "BREAKING" << std::endl << std::endl;
      break;
    }
    // std::cout << "Counter: " << thecounter << std::endl;

  }

  // problem.SetParameterBlockConstant(&theangle);

  ceres::Solver::Options options;
  ceres::Solver::Summary summary;

  options.minimizer_progress_to_stdout = true;
  options.max_num_iterations = 500;
  options.linear_solver_type = ceres::DENSE_SCHUR;
  // options.max_solver_time_in_seconds = 1.0;
  // options.minimizer_type = ceres::LINE_SEARCH;

  ceres::Solve(options, &problem, &summary);

  ceres::Problem::EvaluateOptions pe_options;
  double total_cost = 0.0;
  std::vector<double> residuals;
  pe_options.residual_blocks = resblocks;
  problem.Evaluate(pe_options, &total_cost, &residuals, nullptr, nullptr);
  std::cout << "Residuals" << std::endl;
  for (size_t i = 0; i < residuals.size(); i++) {
    std::cout << i << ": " << residuals[i] << std::endl;
  }


  // std::cout << summary.FullReport() << std::endl;
  std::cout << "Cost: " << summary.final_cost << std::endl;
  std::cout << "Size: " << thesize << std::endl;
  std::cout << "Angle: " << theangle << std::endl;

  rbag.close();
  return 0;
}