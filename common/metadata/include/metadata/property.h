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

#ifndef V4L2_CAMERA_HAL_METADATA_PROPERTY_H_
#define V4L2_CAMERA_HAL_METADATA_PROPERTY_H_

#include "metadata_common.h"
#include "partial_metadata_interface.h"

namespace android {
namespace hardware {
namespace camera {
namespace common {
namespace V1_0 {
namespace metadata {

// A Property is a PartialMetadata that only has a single static tag.
template <typename T>
class Property : public PartialMetadataInterface {
 public:
  Property(int32_t tag, T value) : tag_(tag), value_(std::move(value)){};

  virtual std::vector<int32_t> StaticTags() const override { return {tag_}; };

  virtual std::vector<int32_t> ControlTags() const override { return {}; };

  virtual std::vector<int32_t> DynamicTags() const override { return {}; };

  virtual int PopulateStaticFields(
                              helper::CameraMetadata* metadata) const override {
    return MetadataCommon::UpdateMetadata(metadata, tag_, value_);
  };

  virtual int PopulateDynamicFields(
                              helper::CameraMetadata* metadata) const override {
    (void)metadata;

    return 0;
  };

  virtual int PopulateTemplateRequest(int template_type,
                              helper::CameraMetadata* metadata) const override {
    (void)template_type;
    (void)metadata;

    return 0;
  };

  virtual bool SupportsRequestValues(
                        const helper::CameraMetadata& metadata) const override {
    (void)metadata;

    return true;
  };

  virtual int SetRequestValues(
                              const helper::CameraMetadata& metadata) override {
    (void)metadata;

    return 0;
  };

 private:
  int32_t tag_;
  T value_;
};

} // namespace metadata
} // namespace V1_0
} // namespace common
} // namespace camera
} // namespace hardware
} // namespace android

#endif  // V4L2_CAMERA_HAL_METADATA_PROPERTY_H_
