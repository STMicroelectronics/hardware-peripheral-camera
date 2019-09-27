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

#ifndef ANDROID_HARDWARE_CAMERA_DEVICE_V3_2_CAMERADEVICE_H
#define ANDROID_HARDWARE_CAMERA_DEVICE_V3_2_CAMERADEVICE_H

#include "utils/Mutex.h"

#include <android/hardware/camera/device/3.2/ICameraDevice.h>
#include <hidl/MQDescriptor.h>
#include <hidl/Status.h>

#include "camera_device_session.h"
#include "metadata/metadata.h"
#include "static_properties.h"
#include "v4l2/v4l2_wrapper.h"

namespace android {
namespace hardware {
namespace camera {
namespace device {
namespace V3_2 {
namespace implementation {

using ::android::hardware::camera::common::V1_0::CameraResourceCost;
using ::android::hardware::camera::common::V1_0::Status;
using ::android::hardware::camera::common::V1_0::TorchMode;

using ::android::hardware::camera::common::V1_0::metadata::Metadata;

using ::android::hardware::camera::common::V1_0::v4l2::V4L2Wrapper;

using ::android::hardware::hidl_array;
using ::android::hardware::hidl_memory;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::sp;

struct CameraDevice : public ICameraDevice {
public:
    CameraDevice(const std::string& node);
    int initialize();

    // Methods from ::android::hardware::camera::device::V3_2::ICameraDevice follow.
    Return<void> getResourceCost(getResourceCost_cb _hidl_cb) override;
    Return<void> getCameraCharacteristics(getCameraCharacteristics_cb _hidl_cb) override;
    Return<Status> setTorchMode(TorchMode mode) override;
    Return<void> open(const sp<ICameraDeviceCallback>& callback, open_cb _hidl_cb) override;
    Return<void> dumpState(const hidl_handle& fd) override;

protected:
    Status initStatus() const;

    virtual sp<CameraDeviceSession> createSession(
                                    const sp<ICameraDeviceCallback>& callback);

protected:
    const std::string camera_node_;

    bool init_fail_;
    bool disconnected_;

    wp<CameraDeviceSession> session_;
    std::shared_ptr<V4L2Wrapper> v4l2_wrapper_;
    std::shared_ptr<Metadata> metadata_;
    std::shared_ptr<StaticProperties> static_info_;

    mutable Mutex lock_;

};

}  // namespace implementation
}  // namespace V3_2
}  // namespace device
}  // namespace camera
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_CAMERA_DEVICE_V3_2_CAMERADEVICE_H
