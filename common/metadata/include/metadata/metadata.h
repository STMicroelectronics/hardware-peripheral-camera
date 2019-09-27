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

#ifndef V4L2_CAMERA_HAL_METADATA_H_
#define V4L2_CAMERA_HAL_METADATA_H_

#include <set>

#include <CameraMetadata.h>

#include "metadata_common.h"

namespace android {
namespace hardware {
namespace camera {
namespace common {
namespace V1_0 {
namespace metadata {

class Metadata {
 public:
  Metadata(PartialMetadataSet components);
  virtual ~Metadata();

  int FillStaticMetadata(helper::CameraMetadata* metadata);
  bool IsValidRequest(const helper::CameraMetadata& metadata) const;
  int GetRequestTemplate(int template_type,
                         helper::CameraMetadata* template_metadata) const;
  int SetRequestSettings(const helper::CameraMetadata& metadata);
  int FillResultMetadata(helper::CameraMetadata* metadata);

 private:
  // The overall metadata is broken down into several distinct pieces.
  // Note: it is undefined behavior if multiple components share tags.
  PartialMetadataSet components_;

  Metadata(const Metadata&);
  void operator=(const Metadata&);
};

} // namespace metadata
} // namespace V1_0
} // namespace common
} // namespace camera
} // namespace hardware
} // namespace android

#endif  // V4L2_CAMERA_HAL_V4L2_METADATA_H_
