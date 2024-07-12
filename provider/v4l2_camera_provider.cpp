/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "v4l2_camera_provider.h"

// #define LOG_NDEBUG 0
#include <log/log.h>

#include "v4l2_camera_device.h"

namespace android {
namespace hardware {
namespace camera {
namespace provider {
namespace implementation {

using aidl::android::hardware::camera::common::CameraDeviceStatus;

using ::android::hardware::camera::device::implementation::V4l2CameraConfig;
using ::android::hardware::camera::device::implementation::V4l2StreamConfig;
using ::android::hardware::camera::device::implementation::V4l2CameraDevice;

const std::string V4l2CameraProvider::kProviderName = "internal";
// "device@<version>/internal/<id>"
const std::regex V4l2CameraProvider::kDeviceNameRegex(
    "device@([0-9]+\\.[0-9]+)/internal/(.+)");

std::shared_ptr<V4l2CameraProvider> V4l2CameraProvider::Create() {
  std::shared_ptr<V4l2CameraProvider> provider =
      ndk::SharedRefBase::make<V4l2CameraProvider>();

  if (provider == nullptr) {
    ALOGE("%s: cannot create V4l2CameraProvider !", __func__);
    return nullptr;
  }

  Status res = provider->initialize();
  if (res != Status::OK) {
    ALOGE("%s: Initializing V4l2CameraProvider failed !", __func__);
    return nullptr;
  }

  return provider;
}

V4l2CameraProvider::V4l2CameraProvider()
  : callback_(),
    vendor_tag_sections_(),
    v4l2_cameras_()
{ }

V4l2CameraProvider::~V4l2CameraProvider()
{ }

Status V4l2CameraProvider::initialize() {
  ALOGV("%s: enter", __func__);

  V4l2CameraConfig config;
  config.id = 123456789;
  config.resource_cost = 100;
  config.conflicting_devices.clear();

  /* configure AUX */
  V4l2StreamConfig stream_config;
  memset(&stream_config, 0, sizeof(stream_config));
  stream_config.usage = V4l2StreamConfig::Preview;
  stream_config.num_buffers = 4;
  stream_config.implementation_defined_format = V4L2_PIX_FMT_RGB565;
  property_get("vendor.camera.aux.device", stream_config.node, "");
  if (strlen(stream_config.node) == 0) {
    ALOGE("%s: cannot get aux v4l2 device !", __func__);
    return Status::INTERNAL_ERROR;
  }
  config.streams.push_back(stream_config);

  /* configure main */
  memset(&stream_config, 0, sizeof(stream_config));
  stream_config.usage = V4l2StreamConfig::Capture;
  stream_config.num_buffers = 1;
  stream_config.implementation_defined_format = V4L2_PIX_FMT_RGB565;
  property_get("vendor.camera.main.device", stream_config.node, "");
  if (strlen(stream_config.node) == 0) {
    ALOGE("%s: cannot get main v4l2 device !", __func__);
    return Status::INTERNAL_ERROR;
  }
  config.streams.push_back(stream_config);

  std::string name = "device@" + V4l2CameraDevice::kDeviceVersion +
                     "/" + kProviderName + "/" + std::to_string(config.id);

  v4l2_cameras_[name] = std::move(config);

  return Status::OK;
}

ScopedAStatus V4l2CameraProvider::setCallback(
    const std::shared_ptr<ICameraProviderCallback>& callback) {
  ALOGV("%s: enter", __func__);

  if (callback == nullptr) {
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Status::ILLEGAL_ARGUMENT));
  }

  callback_ = callback;

  for (auto const &camera : v4l2_cameras_) {
    ALOGI("%s: add new camera: %s", __func__, camera.first.c_str());
    callback_->cameraDeviceStatusChange(camera.first, CameraDeviceStatus::PRESENT);
  }

  return ScopedAStatus::ok();
}

ScopedAStatus V4l2CameraProvider::getVendorTags(
    std::vector<VendorTagSection>* vts) {
  ALOGV("%s: enter", __func__);

  if (vts == nullptr) {
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Status::ILLEGAL_ARGUMENT));
  }

  *vts = vendor_tag_sections_;

  return ScopedAStatus::ok();
}

ScopedAStatus V4l2CameraProvider::getCameraIdList(
    std::vector<std::string>* camera_ids) {
  ALOGV("%s: enter", __func__);

  if (camera_ids == nullptr) {
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Status::ILLEGAL_ARGUMENT));
  }

  // There is no internal camera
  camera_ids->clear();

  return ScopedAStatus::ok();
}

static bool parseDeviceName(const std::string &device_name,
                            std::string* device_version,
                            std::string* camera_id) {
  ALOGV("%s: enter", __func__);

  std::string device_name_std(device_name.c_str());
  std::smatch sm;

  if (std::regex_match(device_name_std, sm,
                       V4l2CameraProvider::kDeviceNameRegex)) {
    if (device_version != nullptr)
      *device_version = sm[1];
    if (camera_id != nullptr)
      *camera_id = sm[2];

    return true;
  }

  return false;
}

ScopedAStatus V4l2CameraProvider::getCameraDeviceInterface(
    const std::string& camera_device_name,
    std::shared_ptr<ICameraDevice>* device) {
  ALOGV("%s: enter", __func__);

  if (device == nullptr) {
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Status::ILLEGAL_ARGUMENT));
  }

  std::string version;
  std::string id;
  bool res = parseDeviceName(camera_device_name, &version, &id);
  if (!res) {
    ALOGE("%s: Device name parse fail !", __func__);
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Status::ILLEGAL_ARGUMENT));
  }

  if (version != V4l2CameraDevice::kDeviceVersion) {
    ALOGE("%s: Interface version %s not supported for camera %s !",
        __func__, version.c_str(), id.c_str());
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Status::OPERATION_NOT_SUPPORTED));
  }

  auto it = v4l2_cameras_.find(camera_device_name);
  if (it == v4l2_cameras_.cend()) {
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Status::ILLEGAL_ARGUMENT));
  }

  *device = V4l2CameraDevice::Create(it->second);

  return ScopedAStatus::ok();
}

ScopedAStatus V4l2CameraProvider::notifyDeviceStateChange(
    int64_t device_state) {
  ALOGV("%s: enter", __func__);

  // ignore notif
  (void)(device_state);

  return ScopedAStatus::ok();
}

ScopedAStatus V4l2CameraProvider::getConcurrentCameraIds(
    std::vector<ConcurrentCameraIdCombination>* concurrent_camera_ids) {
  ALOGV("%s: enter", __func__);

  if (concurrent_camera_ids == nullptr) {
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Status::ILLEGAL_ARGUMENT));
  }

  /* We don't support concurrent stream */
  concurrent_camera_ids->clear();

  return ScopedAStatus::ok();
}

ScopedAStatus V4l2CameraProvider::isConcurrentStreamCombinationSupported(
    const std::vector<CameraIdAndStreamCombination>& configs,
    bool* support) {
  ALOGV("%s: enter", __func__);

  if (support == nullptr) {
    return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Status::ILLEGAL_ARGUMENT));
  }

  /* We don't support concurrent stream */
  (void)(configs);
  *support = false;

  return ScopedAStatus::fromServiceSpecificError(
              static_cast<int32_t>(Status::OPERATION_NOT_SUPPORTED));
}

} // implementation
} // provider
} // camera
} // hardware
} // android

