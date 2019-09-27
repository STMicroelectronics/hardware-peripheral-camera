/*
 * Copyright (C) 2019 The Android Open Source Project
 * Copyright (C) 2019 STMicroelectronics
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef HAL_USB_COMMON_TYPES_H_
#define HAL_USB_COMMON_TYPES_H_

#include <string>
#include <vector>

namespace android {
namespace hardware {
namespace camera {
namespace common {
namespace V1_0 {
namespace arc {

struct DeviceInfo {
  // ex: /dev/video0
  std::string device_path;
  // USB vender id
  std::string usb_vid;
  // USB product id
  std::string usb_pid;
  // Some cameras need to wait several frames to output correct images.
  uint32_t frames_to_skip_after_streamon;

  // Member definitions can be found in https://developer.android.com/
  // reference/android/hardware/camera2/CameraCharacteristics.html
  uint32_t lens_facing;
  int32_t sensor_orientation;
  float horizontal_view_angle_16_9;
  float horizontal_view_angle_4_3;
  std::vector<float> lens_info_available_focal_lengths;
  float lens_info_minimum_focus_distance;
  float lens_info_optimal_focus_distance;
  float vertical_view_angle_16_9;
  float vertical_view_angle_4_3;
};

typedef std::vector<DeviceInfo> DeviceInfos;

inline std::string FormatToString(int32_t format) {
  return std::string(reinterpret_cast<char*>(&format), 4);
}

} // namespace arc
} // namespace V1_0
} // namespace common
} // namespace camera
} // namespace hardware
} // namespace android

#endif  // HAL_USB_COMMON_TYPES_H_
