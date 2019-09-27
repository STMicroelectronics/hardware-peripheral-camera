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

#ifndef V4L2_CAMERA_HAL_METADATA_BOOTTIME_STATE_DELEGATE_H_
#define V4L2_CAMERA_HAL_METADATA_BOOTTIME_STATE_DELEGATE_H_

#include "state_delegate_interface.h"

namespace android {
namespace hardware {
namespace camera {
namespace common {
namespace V1_0 {
namespace metadata {

// A StateDelegate is simply a dynamic value that can be queried.
// The value may change between queries.
class BoottimeStateDelegate : public StateDelegateInterface<int64_t> {
 public:
  BoottimeStateDelegate(){};
  ~BoottimeStateDelegate(){};

  int GetValue(int64_t* value) override;
};

} // namespace metadata
} // namespace V1_0
} // namespace common
} // namespace camera
} // namespace hardware
} // namespace android

#endif  // V4L2_CAMERA_HAL_METADATA_BOOTTIME_STATE_DELEGATE_H_
