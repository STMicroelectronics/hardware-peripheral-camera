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

#ifndef CAMERA_DEVICE_IMPLEMENTATION_METADATA_FACTORY_H_
#define CAMERA_DEVICE_IMPLEMENTATION_METADATA_FACTORY_H_

#include <algorithm>
#include <iterator>
#include <memory>
#include <set>

#include "v4l2/v4l2_wrapper.h"
#include "metadata/metadata.h"

namespace android {
namespace hardware {
namespace camera {
namespace device {
namespace V3_2 {
namespace implementation {

using namespace ::android::hardware::camera::common::V1_0::metadata;

using ::android::hardware::camera::common::V1_0::v4l2::V4L2Wrapper;
using ::android::hardware::graphics::common::V1_0::PixelFormat;

class MetadataFactory {

public:

  static int GetV4L2Metadata(std::shared_ptr<V4L2Wrapper> device,
                             std::unique_ptr<Metadata> *result);

private:

  static int AddFormatComponents(std::shared_ptr<V4L2Wrapper> device,
                     std::insert_iterator<PartialMetadataSet> insertion_point);

private:

  static const camera_metadata_rational_t kAeCompensationUnit;
  static const int64_t kV4L2ExposureTimeStepNs;
  static const int32_t kV4L2SensitivityDenominator;
  static const size_t kV4L2MaxJpegSize;

};

} // namespace implementation
} // namespace V3_2
} // namespace device
} // namespace camera
} // namespace hardware
} // namespace android

#endif /* CAMERA_DEVICE_IMPLEMENTATION_METADATA_FACTORY_H_ */
