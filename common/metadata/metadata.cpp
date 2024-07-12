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

#include "metadata.h"

#include "metadata_common.h"

namespace android {
namespace hardware {
namespace camera {
namespace common {
namespace V1_0 {
namespace metadata {

Metadata::Metadata(PartialMetadataSet components)
    : components_(std::move(components)) {
  ALOGV("%s: enter", __FUNCTION__);
}

Metadata::~Metadata() {
  ALOGV("%s: enter", __FUNCTION__);
}

int Metadata::FillStaticMetadata(helper::CameraMetadata* metadata) {
  ALOGV("%s: enter", __FUNCTION__);
  if (!metadata) {
    ALOGE("%s: Can't fill null metadata.", __FUNCTION__);
    return -EINVAL;
  }

  std::vector<int32_t> static_tags;
  std::vector<int32_t> control_tags;
  std::vector<int32_t> dynamic_tags;
  int res = 0;

  for (auto& component : components_) {
    // Prevent components from potentially overriding others.
    helper::CameraMetadata additional_metadata;
    // Populate the fields.
    res = component->PopulateStaticFields(&additional_metadata);

    if (res) {
      ALOGE("%s: Failed to get all static properties.", __FUNCTION__);
      return res;
    }

    // Add it to the overall result.
    if (!additional_metadata.isEmpty()) {
      res = metadata->append(additional_metadata);

      if (res != android::OK) {
        ALOGE("%s: Failed to append all static properties.", __FUNCTION__);
        return res;
      }
    }

    // Note what tags the component adds.
    std::vector<int32_t> tags = component->StaticTags();
    std::move(tags.begin(),
              tags.end(),
              std::inserter(static_tags, static_tags.end()));
    tags = component->ControlTags();
    std::move(tags.begin(),
              tags.end(),
              std::inserter(control_tags, control_tags.end()));
    tags = component->DynamicTags();
    std::move(tags.begin(),
              tags.end(),
              std::inserter(dynamic_tags, dynamic_tags.end()));
  }

  // Populate the meta fields.
  static_tags.push_back(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS);
  res = MetadataCommon::UpdateMetadata(metadata,
                          ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS, control_tags);

  if (res != android::OK) {
    ALOGE("%s: Failed to add request keys meta key.", __FUNCTION__);
    return -ENODEV;
  }

  static_tags.push_back(ANDROID_REQUEST_AVAILABLE_RESULT_KEYS);
  res = MetadataCommon::UpdateMetadata(metadata,
                          ANDROID_REQUEST_AVAILABLE_RESULT_KEYS, dynamic_tags);

  if (res != android::OK) {
    ALOGE("%s: Failed to add result keys meta key.", __FUNCTION__);
    return -ENODEV;
  }

  static_tags.push_back(ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS);
  res = MetadataCommon::UpdateMetadata(metadata,
                  ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS, static_tags);

  if (res != android::OK) {
    ALOGE("%s: Failed to add characteristics keys meta key.", __FUNCTION__);
    return -ENODEV;
  }

  // TODO(b/31018853): cache result.
  return 0;
}

bool Metadata::IsValidRequest(const helper::CameraMetadata& metadata) const {
  ALOGV("%s: enter", __FUNCTION__);

  // Empty means "use previous settings", which are inherently valid.
  if (metadata.isEmpty())
    return true;

  for (auto& component : components_) {
    // Check that all components support the values requested of them.
    bool valid_request = component->SupportsRequestValues(metadata);

    if (!valid_request) {
      // Exit early if possible.
      return false;
    }
  }

  return true;
}

int Metadata::GetRequestTemplate(int template_type,
                               helper::CameraMetadata* template_metadata) const{
  ALOGV("%s: enter", __FUNCTION__);

  if (!template_metadata) {
    ALOGE("%s: Can't fill null template.", __FUNCTION__);
    return -EINVAL;
  }

  // Templates are numbered 1 through COUNT-1 for some reason.
  if (template_type < 1 ||
                      template_type >= MetadataCommon::kRequestTemplateCount) {
    ALOGE("%s: Unrecognized template type %d.", __FUNCTION__, template_type);
    return -EINVAL;
  }

  for (auto& component : components_) {
    // Prevent components from potentially overriding others.
    helper::CameraMetadata additional_metadata;
    int res =
        component->PopulateTemplateRequest(template_type, &additional_metadata);
    if (res) {
      ALOGE("%s: Failed to get all default request fields.", __FUNCTION__);
      return res;
    }

    // Add it to the overall result.
    if (!additional_metadata.isEmpty()) {
      res = template_metadata->append(additional_metadata);

      if (res != android::OK) {
        ALOGE("%s: Failed to append all default request fields.", __FUNCTION__);
        return res;
      }
    }
  }

  // TODO(b/31018853): cache result.
  return 0;
}

int Metadata::SetRequestSettings(const helper::CameraMetadata& metadata) {
  ALOGV("%s: enter", __FUNCTION__);

  // Empty means "use previous settings".
  if (metadata.isEmpty())
    return 0;

  for (auto& component : components_) {
    int res = component->SetRequestValues(metadata);

    if (res) {
      ALOGE("%s: Failed to set all requested settings.", __FUNCTION__);
      return res;
    }
  }

  return 0;
}

int Metadata::FillResultMetadata(helper::CameraMetadata* metadata) {
  ALOGV("%s: enter", __FUNCTION__);

  if (!metadata) {
    ALOGE("%s: Can't fill null metadata.", __FUNCTION__);
    return -EINVAL;
  }

  for (auto& component : components_) {
    // Prevent components from potentially overriding others.
    helper::CameraMetadata additional_metadata;
    int res = component->PopulateDynamicFields(&additional_metadata);

    if (res) {
      ALOGE("%s: Failed to get all dynamic result fields.", __FUNCTION__);
      return res;
    }

    // Add it to the overall result.
    if (!additional_metadata.isEmpty()) {
      res = metadata->append(additional_metadata);

      if (res != android::OK) {
        ALOGE("%s: Failed to append all dynamic result fields.", __FUNCTION__);
        return res;
      }
    }
  }

  return 0;
}

} // namespace metadata
} // namespace V1_0
} // namespace common
} // namespace camera
} // namespace hardware
} // namespace android
