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

#ifndef V4L2_CAMERA_HAL_METADATA_MAP_CONVERTER_H_
#define V4L2_CAMERA_HAL_METADATA_MAP_CONVERTER_H_

#define LOG_TAG "android.hardware.camera.common@1.0-metadata.stm32mpu"
// #define LOG_NDEBUG 0

#include <errno.h>

#include <map>
#include <memory>

#include <utils/Log.h>

#include "converter_interface.h"

namespace android {
namespace hardware {
namespace camera {
namespace common {
namespace V1_0 {
namespace metadata {

// A MapConverter fits values converted by a wrapped converter
// to a map entry corresponding to the key with the nearest value.
template <typename TMetadata, typename TV4L2, typename TMapKey>
class MapConverter : public ConverterInterface<TMetadata, TV4L2> {
 public:
  MapConverter(
      std::shared_ptr<ConverterInterface<TMetadata, TMapKey>> wrapped_converter,
      std::map<TMapKey, TV4L2> conversion_map);

  virtual int MetadataToV4L2(TMetadata value, TV4L2* conversion) override;
  virtual int V4L2ToMetadata(TV4L2 value, TMetadata* conversion) override;

 private:
  std::shared_ptr<ConverterInterface<TMetadata, TMapKey>> wrapped_converter_;
  std::map<TMapKey, TV4L2> conversion_map_;

  MapConverter(const MapConverter&);
  void operator=(const MapConverter&);
};

// -----------------------------------------------------------------------------

template <typename TMetadata, typename TV4L2, typename TMapKey>
MapConverter<TMetadata, TV4L2, TMapKey>::MapConverter(
    std::shared_ptr<ConverterInterface<TMetadata, TMapKey>> wrapped_converter,
    std::map<TMapKey, TV4L2> conversion_map)
    : wrapped_converter_(std::move(wrapped_converter)),
      conversion_map_(conversion_map) {
  ALOGV("%s: enter", __FUNCTION__);
}

template <typename TMetadata, typename TV4L2, typename TMapKey>
int MapConverter<TMetadata, TV4L2, TMapKey>::MetadataToV4L2(TMetadata value,
                                                            TV4L2* conversion) {
  ALOGV("%s: enter", __FUNCTION__);

  if (conversion_map_.empty()) {
    ALOGE("%s: Empty conversion map.", __FUNCTION__);
    return -EINVAL;
  }

  TMapKey raw_conversion = 0;
  int res = wrapped_converter_->MetadataToV4L2(value, &raw_conversion);
  if (res) {
    ALOGE("%s: Failed to perform underlying conversion.", __FUNCTION__);
    return res;
  }

  // Find nearest key.
  auto kv = conversion_map_.lower_bound(raw_conversion);
  // lower_bound finds the first >= element.
  if (kv == conversion_map_.begin()) {
    // Searching for less than the smallest key, so that will be the nearest.
    *conversion = kv->second;
  } else if (kv == conversion_map_.end()) {
    // Searching for greater than the largest key, so that will be the nearest.
    --kv;
    *conversion = kv->second;
  } else {
    // Since kv points to the first >= element, either that or the previous
    // element will be nearest.
    *conversion = kv->second;
    TMapKey diff = kv->first - raw_conversion;

    // Now compare to the previous. This element will be < raw conversion,
    // so reverse the order of the subtraction.
    --kv;
    if (raw_conversion - kv->first < diff) {
      *conversion = kv->second;
    }
  }

  return 0;
}

template <typename TMetadata, typename TV4L2, typename TMapKey>
int MapConverter<TMetadata, TV4L2, TMapKey>::V4L2ToMetadata(
    TV4L2 value, TMetadata* conversion) {
  ALOGV("%s: enter", __FUNCTION__);

  // Unfortunately no bi-directional map lookup in C++.
  // Breaking on second, not first found so that a warning
  // can be given if there are multiple values.
  size_t count = 0;
  int res;
  for (auto kv : conversion_map_) {
    if (kv.second == value) {
      ++count;
      if (count == 1) {
        // First match.
        res = wrapped_converter_->V4L2ToMetadata(kv.first, conversion);
      } else {
        // second match.
        break;
      }
    }
  }

  if (count == 0) {
    ALOGE("%s: Couldn't find map conversion of V4L2 value %d.",
              __FUNCTION__, value);
    return -EINVAL;
  } else if (count > 1) {
    ALOGW("%s: Multiple map conversions found for V4L2 value %d, using first.",
             __FUNCTION__, value);
  }
  return res;
}

} // namespace metadata
} // namespace V1_0
} // namespace common
} // namespace camera
} // namespace hardware
} // namespace android

#endif  // V4L2_CAMERA_HAL_METADATA_MAP_CONVERTER_H_
