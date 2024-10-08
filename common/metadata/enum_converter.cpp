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

#define LOG_TAG "android.hardware.camera.common@1.0-metadata.stm32mpu"
// #define LOG_NDEBUG 0

#include "enum_converter.h"

#include <errno.h>

#include <utils/Log.h>

namespace android {
namespace hardware {
namespace camera {
namespace common {
namespace V1_0 {
namespace metadata {

EnumConverter::EnumConverter(
    const std::multimap<int32_t, uint8_t>& v4l2_to_metadata)
    : v4l2_to_metadata_(v4l2_to_metadata) {
  ALOGV("%s: enter", __FUNCTION__);
}

int EnumConverter::MetadataToV4L2(uint8_t value, int32_t* conversion) {
  // Unfortunately no bi-directional map lookup in C++.
  // Breaking on second, not first found so that a warning
  // can be given if there are multiple values.
  size_t count = 0;
  for (auto kv : v4l2_to_metadata_) {
    if (kv.second == value) {
      ++count;
      if (count == 1) {
        // First match.
        *conversion = kv.first;
      } else {
        // second match.
        break;
      }
    }
  }

  if (count == 0) {
    ALOGV("%s: Couldn't find V4L2 conversion of metadata value %d.",
              __FUNCTION__, value);
    return -EINVAL;
  } else if (count > 1) {
    ALOGV(
      "%s: Multiple V4L2 conversions found for metadata value %d, using first.",
           __FUNCTION__, value);
  }
  return 0;
}

int EnumConverter::V4L2ToMetadata(int32_t value, uint8_t* conversion) {
  auto element_range = v4l2_to_metadata_.equal_range(value);
  if (element_range.first == element_range.second) {
    ALOGV("%s: Couldn't find metadata conversion of V4L2 value %d.",
              __FUNCTION__, value);
    return -EINVAL;
  }

  auto element = element_range.first;
  *conversion = element->second;

  if (++element != element_range.second) {
    ALOGV(
      "%s: Multiple metadata conversions found for V4L2 value %d, using first.",
          __FUNCTION__, value);
  }
  return 0;
}

} // namespace metadata
} // namespace V1_0
} // namespace common
} // namespace camera
} // namespace hardware
} // namespace android
