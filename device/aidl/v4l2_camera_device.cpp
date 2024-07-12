/*
 * Copyright (C) 2023 STMicroelectronics
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

#include "v4l2_camera_device.h"

#include <log/log.h>

#include <parser/metadata_factory.h>

#define CONFIGURATION_FILE "/vendor/etc/config/metadata_definitions.xml"

namespace android {
namespace hardware {
namespace camera {
namespace device {
namespace implementation {

using aidl::android::hardware::camera::device::StreamType;
using aidl::android::hardware::camera::device::StreamRotation;

const std::string V4l2CameraDevice::kDeviceVersion = "1.1";

std::shared_ptr<V4l2CameraDevice> V4l2CameraDevice::Create(const V4l2CameraConfig &config) {
  std::shared_ptr<V4l2CameraDevice> device =
      ndk::SharedRefBase::make<V4l2CameraDevice>(config);

  if (device == nullptr) {
    ALOGE("%s (%d): Cannot create V4l2CameraDevice !", __func__, config.id);
    return nullptr;
  }

  Status res = device->initialize();
  if (res != Status::OK) {
    ALOGE("%s (%d): Initializing V4l2CameraDevice failed !",
        __func__, config.id);
    return nullptr;
  }

  return device;
}

V4l2CameraDevice::V4l2CameraDevice(const V4l2CameraConfig &config)
  : config_(config)
{ }

V4l2CameraDevice::~V4l2CameraDevice()
{ }

Status V4l2CameraDevice::initialize() {
  MetadataFactory factory;

  int err = factory.load(CONFIGURATION_FILE);
  if (err != 0) {
    ALOGE("%s: cannot load xml configuration file '%s': %d",
              __func__, CONFIGURATION_FILE, err);
    return Status::INTERNAL_ERROR;
  }

  std::unique_ptr<Metadata> metadata;
  err = factory.parse(metadata);
  if (err != 0) {
    ALOGE("%s: cannot parse xml configuration file '%s': %d",
              __func__, CONFIGURATION_FILE, err);
    return Status::INTERNAL_ERROR;
  }

  metadata_ = std::move(metadata);

  /* Get static metadata */
  std::unique_ptr<CameraMetadataHelper> out =
                                      std::make_unique<CameraMetadataHelper>();

  err = metadata_->FillStaticMetadata(out.get());
  if (err) {
    ALOGE("%s: failed to get static metadata: %d", __func__, err);
    return Status::INTERNAL_ERROR;
  }

  static_info_.reset(StaticProperties::NewStaticProperties(std::move(out)));
  if (!static_info_) {
    ALOGE("%s: failed to initialize static properties from device metadata",
              __func__);
    return Status::INTERNAL_ERROR;
  }

  return Status::OK;
}

ScopedAStatus V4l2CameraDevice::getCameraCharacteristics(
    CameraMetadata* characteristics) {
  if (characteristics == nullptr) {
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Status::ILLEGAL_ARGUMENT));
  }

  const camera_metadata_t *raw_metadata = static_info_->raw_metadata();
  int size = get_camera_metadata_size(raw_metadata);
  const uint8_t *data = reinterpret_cast<const uint8_t *>(raw_metadata);

  characteristics->metadata = std::vector<uint8_t>(data, data + size);

  return ScopedAStatus::ok();
}

ScopedAStatus V4l2CameraDevice::getPhysicalCameraCharacteristics(
    const std::string& physical_camera_id,
    CameraMetadata* characteristics) {
  if (characteristics == nullptr) {
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Status::ILLEGAL_ARGUMENT));
  }

  (void)(physical_camera_id);

  return ScopedAStatus::fromServiceSpecificError(
              static_cast<int32_t>(Status::ILLEGAL_ARGUMENT));
}

ScopedAStatus V4l2CameraDevice::getResourceCost(
    CameraResourceCost* resource_cost) {
  if (resource_cost == nullptr) {
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Status::ILLEGAL_ARGUMENT));
  }

  resource_cost->resourceCost = config_.resource_cost;
  resource_cost->conflictingDevices = config_.conflicting_devices;

  return ScopedAStatus::ok();
}

ScopedAStatus V4l2CameraDevice::isStreamCombinationSupported(
    const StreamConfiguration& config, bool* supported) {
  if (supported == nullptr) {
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Status::ILLEGAL_ARGUMENT));
  }

  if (!static_info_->StreamConfigurationSupported(&config)) {
    *supported = false;
    return ScopedAStatus::ok();
  }

  *supported = true;

  return ScopedAStatus::ok();
}

ScopedAStatus V4l2CameraDevice::open(
    const std::shared_ptr<ICameraDeviceCallback>& callback,
    std::shared_ptr<ICameraDeviceSession>* session) {
  if (session == nullptr) {
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Status::ILLEGAL_ARGUMENT));
  }

  *session = V4l2CameraDeviceSession::Create(config_, metadata_,
                                             static_info_, callback);
  if (*session == nullptr) {
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Status::INTERNAL_ERROR));
  }

  return ScopedAStatus::ok();
}

ScopedAStatus V4l2CameraDevice::openInjectionSession(
    const std::shared_ptr<ICameraDeviceCallback>& callback,
    std::shared_ptr<ICameraInjectionSession>* session) {
  if (session == nullptr) {
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Status::ILLEGAL_ARGUMENT));
  }

  (void)(callback);

  return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Status::OPERATION_NOT_SUPPORTED));
}

ScopedAStatus V4l2CameraDevice::setTorchMode(bool on) {
  (void)(on);

  return ScopedAStatus::fromServiceSpecificError(
              static_cast<int32_t>(Status::OPERATION_NOT_SUPPORTED));
}

ScopedAStatus V4l2CameraDevice::turnOnTorchWithStrengthLevel(
    int32_t torchStrength) {
  (void)(torchStrength);

  return ScopedAStatus::fromServiceSpecificError(
              static_cast<int32_t>(Status::OPERATION_NOT_SUPPORTED));
}

ScopedAStatus V4l2CameraDevice::getTorchStrengthLevel(
    int32_t* strength_level) {
  if (strength_level == nullptr) {
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Status::ILLEGAL_ARGUMENT));
  }

  return ScopedAStatus::fromServiceSpecificError(
              static_cast<int32_t>(Status::OPERATION_NOT_SUPPORTED));
}

} // implementation
} // device
} // camera
} // hardware
} // android


