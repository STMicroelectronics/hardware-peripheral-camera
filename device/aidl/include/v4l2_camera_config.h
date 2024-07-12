/*
 * Copyright (C) 2023 STMicroelectronics
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

#ifndef AIDL_ANDROID_HARDWARE_CAMERA_PROVIDER_V4L2_CAMERA_CONFIG_H
#define AIDL_ANDROID_HARDWARE_CAMERA_PROVIDER_V4L2_CAMERA_CONFIG_H

#include "v4l2_stream_config.h"

#include <list>
#include <vector>

namespace android {
namespace hardware {
namespace camera {
namespace device {
namespace implementation {

struct V4l2CameraConfig {
  uint32_t id;

  uint8_t resource_cost;
  std::vector<std::string> conflicting_devices;

  std::list<V4l2StreamConfig> streams;
};

} // implementation
} // device
} // camera
} // hardware
} // android

#endif // AIDL_ANDROID_HARDWARE_CAMERA_PROVIDER_V4L2_CAMERA_CONFIG_H
