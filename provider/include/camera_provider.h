/*
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

#ifndef ANDROID_HARDWARE_CAMERA_PROVIDER_V2_4_CAMERAPROVIDER_H
#define ANDROID_HARDWARE_CAMERA_PROVIDER_V2_4_CAMERAPROVIDER_H

#include <memory>
#include <unordered_map>
#include <vector>

#include <android/hardware/camera/provider/2.4/ICameraProvider.h>
#include <hidl/MQDescriptor.h>
#include <hidl/Status.h>
#include "v4l2/v4l2_wrapper.h"

namespace android {
namespace hardware {
namespace camera {
namespace provider {
namespace V2_4 {
namespace implementation {

using ::android::hardware::hidl_array;
using ::android::hardware::hidl_memory;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::sp;

struct CameraProvider : public ICameraProvider {
  CameraProvider();

  void initialize();

  // Methods from ::android::hardware::camera::provider::V2_4::ICameraProvider follow.
  Return<common::V1_0::Status> setCallback(
      const sp<provider::V2_4::ICameraProviderCallback>& callback) override;
  Return<void> getVendorTags(getVendorTags_cb _hidl_cb) override;
  Return<void> getCameraIdList(getCameraIdList_cb _hidl_cb) override;
  Return<void> isSetTorchModeSupported(
      isSetTorchModeSupported_cb _hidl_cb) override;
  Return<void> getCameraDeviceInterface_V1_x(
      const hidl_string& camera_device_name,
      getCameraDeviceInterface_V1_x_cb _hidl_cb) override;
  Return<void> getCameraDeviceInterface_V3_x(
      const hidl_string& camera_device_name,
      getCameraDeviceInterface_V3_x_cb _hidl_cb) override;

private:
  std::string getHidlDeviceName(std::string camera_id);

private:
  sp<ICameraProviderCallback> callback_;

  hidl_vec<common::V1_0::VendorTagSection> vendor_tag_sections_;

  std::unordered_map<std::string, std::string> camera_nodes_;

  const std::string kVideoDev = "/dev/video";

};

}  // namespace implementation
}  // namespace V2_4
}  // namespace provider
}  // namespace camera
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_CAMERA_PROVIDER_V2_4_CAMERAPROVIDER_H
