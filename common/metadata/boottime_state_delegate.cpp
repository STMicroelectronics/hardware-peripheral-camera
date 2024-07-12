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

#include <errno.h>
#include <string.h>
#include <time.h>

#include "boottime_state_delegate.h"

namespace android {
namespace hardware {
namespace camera {
namespace common {
namespace V1_0 {
namespace metadata {

int BoottimeStateDelegate::GetValue(int64_t* value) {
  struct timespec ts;

  int res = clock_gettime(CLOCK_BOOTTIME, &ts);
  if (res) {
    ALOGE("%s: Failed to get BOOTTIME for state delegate: %d (%s)",
              __FUNCTION__, errno, strerror(errno));
    return -errno;
  }
  *value = ts.tv_sec * 1000000000ULL + ts.tv_nsec;

  ALOGV("%s: Boottime: %ld", __func__, *value);

  return 0;
}

} // namespace metadata
} // namespace V1_0
} // namespace common
} // namespace camera
} // namespace hardware
} // namespace android
