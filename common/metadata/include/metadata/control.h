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

#ifndef V4L2_CAMERA_HAL_METADATA_CONTROL_H_
#define V4L2_CAMERA_HAL_METADATA_CONTROL_H_

#include <vector>

#include <system/camera_metadata.h>

#include "metadata_common.h"
#include "partial_metadata_interface.h"
#include "tagged_control_delegate.h"
#include "tagged_control_options.h"

namespace android {
namespace hardware {
namespace camera {
namespace common {
namespace V1_0 {
namespace metadata {

// A Control is a PartialMetadata with values that can be gotten/set.
template <typename T>
class Control : public PartialMetadataInterface {
 public:
  // Options are optional (i.e. nullable), delegate is not.
  Control(std::unique_ptr<TaggedControlDelegate<T>> delegate,
          std::unique_ptr<TaggedControlOptions<T>> options = nullptr);

  virtual std::vector<int32_t> StaticTags() const override;
  virtual std::vector<int32_t> ControlTags() const override;
  virtual std::vector<int32_t> DynamicTags() const override;

  virtual int PopulateStaticFields(
      helper::CameraMetadata* metadata) const override;
  virtual int PopulateDynamicFields(
      helper::CameraMetadata* metadata) const override;
  virtual int PopulateTemplateRequest(
      int template_type, helper::CameraMetadata* metadata) const override;
  virtual bool SupportsRequestValues(
      const helper::CameraMetadata& metadata) const override;
  virtual int SetRequestValues(
      const helper::CameraMetadata& metadata) override;

 private:
  std::unique_ptr<TaggedControlDelegate<T>> delegate_;
  std::unique_ptr<TaggedControlOptions<T>> options_;

  Control(const Control&);
  void operator=(const Control&);
};

// -----------------------------------------------------------------------------

template <typename T>
Control<T>::Control(std::unique_ptr<TaggedControlDelegate<T>> delegate,
                    std::unique_ptr<TaggedControlOptions<T>> options)
    : delegate_(std::move(delegate)), options_(std::move(options)) {}

template <typename T>
std::vector<int32_t> Control<T>::StaticTags() const {
  std::vector<int32_t> result;
  if (options_ && options_->tag() != DO_NOT_REPORT_OPTIONS) {
    result.push_back(options_->tag());
  }
  return result;
}

template <typename T>
std::vector<int32_t> Control<T>::ControlTags() const {
  return {delegate_->tag()};
}

template <typename T>
std::vector<int32_t> Control<T>::DynamicTags() const {
  return {delegate_->tag()};
}

template <typename T>
int Control<T>::PopulateStaticFields(helper::CameraMetadata* metadata) const {
  if (!options_) {
    ALOGV("%s: No options for control %d, nothing to populate.",
                                                __FUNCTION__, delegate_->tag());
    return 0;
  } else if (options_->tag() == DO_NOT_REPORT_OPTIONS) {
    ALOGV(
        "%s: Options for control %d are not reported, "
        "probably are set values defined and already known by the API.",
                                                __FUNCTION__, delegate_->tag());
    return 0;
  }

  return MetadataCommon::UpdateMetadata(metadata, options_->tag(),
                                        options_->MetadataRepresentation());
}

template <typename T>
int Control<T>::PopulateDynamicFields(helper::CameraMetadata* metadata) const {
  /* Populate the current setting. */
  T value;
  int res = delegate_->GetValue(&value);
  if (res) {
    return res;
  }
  return MetadataCommon::UpdateMetadata(metadata, delegate_->tag(), value);
}

template <typename T>
int Control<T>::PopulateTemplateRequest(
    int template_type, helper::CameraMetadata* metadata) const {
  /* Populate with a default. */
  T value;
  int res;
  if (options_) {
    res = options_->DefaultValueForTemplate(template_type, &value);
  } else {
    /* If there's no options (and thus no default option),
     * fall back to whatever the current value is.
     */
    res = delegate_->GetValue(&value);
  }
  if (res) {
    return res;
  }

  return MetadataCommon::UpdateMetadata(metadata, delegate_->tag(), value);
}

template <typename T>
bool Control<T>::SupportsRequestValues(
    const helper::CameraMetadata& metadata) const {
  if (metadata.isEmpty()) {
    /* Implicitly supported. */
    return true;
  }

  /* Get the requested setting for this control. */
  T requested;
  int res = MetadataCommon::SingleTagValue(metadata, delegate_->tag(),
                                           &requested);
  if (res == -ENOENT) {
    /* Nothing requested of this control, that's fine. */
    return true;
  } else if (res) {
    ALOGE("%s: Failure while searching for request value for tag %d",
             __FUNCTION__, delegate_->tag());
    return false;
  }

  /* Check that the requested setting is in the supported options. */
  if (!options_) {
    ALOGV("%s: No options for control %d; request implicitly supported.",
              __FUNCTION__, delegate_->tag());
    return true;
  }
  bool ret = options_->IsSupported(requested);
  if (!ret) {
    const char *name = get_camera_metadata_tag_name(delegate_->tag());
    ALOGE("%s: value not supported for %s", __func__, name ? name : "invalid");
  }

  return ret;
}

template <typename T>
int Control<T>::SetRequestValues(const helper::CameraMetadata& metadata) {
  if (metadata.isEmpty()) {
    /* No changes necessary. */
    return 0;
  }

  /* Get the requested value. */
  T requested;
  int res = MetadataCommon::SingleTagValue(metadata, delegate_->tag(),
                                           &requested);
  if (res == -ENOENT) {
    /* Nothing requested of this control, nothing to do. */
    return 0;
  } else if (res) {
    ALOGE("%s: Failure while searching for request value for tag %d",
              __FUNCTION__, delegate_->tag());
    return res;
  }

  /* Check that the value is supported. */
  if (options_ && !options_->IsSupported(requested)) {
    ALOGE("%s: Unsupported value requested for control %d.",
              __FUNCTION__, delegate_->tag());
    return -EINVAL;
  }

  return delegate_->SetValue(requested);
}

} // namespace metadata
} // namespace V1_0
} // namespace common
} // namespace camera
} // namespace hardware
} // namespace android

#endif  // V4L2_CAMERA_HAL_METADATA_CONTROL_H_
