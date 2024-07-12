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

#include <utils/Log.h>

#include "metadata_common.h"

namespace android {
namespace hardware {
namespace camera {
namespace common {
namespace V1_0 {
namespace metadata {

// GetDataPointer(entry, val)
//
// A helper for other methods in this file.
// Gets the data pointer of a given metadata entry into |*val|.

void MetadataCommon::GetDataPointer(camera_metadata_ro_entry_t& entry,
                                    const uint8_t** val) {
  *val = entry.data.u8;
}

void MetadataCommon::GetDataPointer(camera_metadata_ro_entry_t& entry,
                                    const int32_t** val) {
  *val = entry.data.i32;
}

void MetadataCommon::GetDataPointer(camera_metadata_ro_entry_t& entry,
                                    const float** val) {
  *val = entry.data.f;
}

void MetadataCommon::GetDataPointer(camera_metadata_ro_entry_t& entry,
                                    const int64_t** val) {
  *val = entry.data.i64;
}

void MetadataCommon::GetDataPointer(camera_metadata_ro_entry_t& entry,
                                    const double** val) {
  *val = entry.data.d;
}

void MetadataCommon::GetDataPointer(camera_metadata_ro_entry_t& entry,
                                    const camera_metadata_rational_t** val) {
  *val = entry.data.r;
}

} // namespace metadata
} // namespace V1_0
} // namespace common
} // namespace camera
} // namespace hardware
} // namespace android
