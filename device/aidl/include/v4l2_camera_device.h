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

#ifndef AIDL_ANDROID_HARDWARE_CAMERA_DEVICE_V4L2_CAMERA_DEVICE_H
#define AIDL_ANDROID_HARDWARE_CAMERA_DEVICE_V4L2_CAMERA_DEVICE_H

#include <aidl/android/hardware/camera/common/Status.h>
#include <aidl/android/hardware/camera/device/BnCameraDevice.h>
#include <aidl/android/hardware/camera/device/ICameraDeviceCallback.h>

#include "v4l2_camera_config.h"
#include "v4l2_camera_device_session.h"
#include "static_properties.h"

namespace android {
namespace hardware {
namespace camera {
namespace device {
namespace implementation {

using aidl::android::hardware::camera::common::Status;
using aidl::android::hardware::camera::common::CameraResourceCost;
using aidl::android::hardware::camera::device::BnCameraDevice;
using aidl::android::hardware::camera::device::CameraMetadata;
using aidl::android::hardware::camera::device::ICameraDevice;
using aidl::android::hardware::camera::device::ICameraDeviceCallback;
using aidl::android::hardware::camera::device::ICameraDeviceSession;
using aidl::android::hardware::camera::device::ICameraInjectionSession;
using aidl::android::hardware::camera::device::StreamConfiguration;

using ndk::ScopedAStatus;

class V4l2CameraDevice : public BnCameraDevice {
public:
  static const std::string kDeviceVersion;
  static std::shared_ptr<V4l2CameraDevice> Create(
      const V4l2CameraConfig &config);

  V4l2CameraDevice(const V4l2CameraConfig &config);
  virtual ~V4l2CameraDevice();

  // Override functions in ICameraDevice
  ScopedAStatus getCameraCharacteristics(
      CameraMetadata* characteristics) override;

  ScopedAStatus getPhysicalCameraCharacteristics(
      const std::string& physical_camera_id,
      CameraMetadata* characteristics) override;

  ScopedAStatus getResourceCost(CameraResourceCost* resource_cost) override;

  ScopedAStatus isStreamCombinationSupported(
      const StreamConfiguration& streams, bool* supported) override;

  ScopedAStatus open(const std::shared_ptr<ICameraDeviceCallback>& callback,
                     std::shared_ptr<ICameraDeviceSession>* session) override;

  ScopedAStatus openInjectionSession(
      const std::shared_ptr<ICameraDeviceCallback>& callback,
      std::shared_ptr<ICameraInjectionSession>* session) override;

  ScopedAStatus setTorchMode(bool on) override;

  ScopedAStatus turnOnTorchWithStrengthLevel(int32_t torchStrength) override;

  ScopedAStatus getTorchStrengthLevel(int32_t* strength_level) override;

  // End of override functions in ICameraDevice

private:
  Status initialize();

private:
  V4l2CameraConfig config_;
  std::shared_ptr<V4l2CameraDeviceSession> session_;
  std::shared_ptr<Metadata> metadata_;
  std::shared_ptr<StaticProperties> static_info_;
};

} // implementation
} // device
} // camera
} // hardware
} // android

#endif // AIDL_ANDROID_HARDWARE_CAMERA_DEVICE_V4L2_CAMERA_DEVICE_H
