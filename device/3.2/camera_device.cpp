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

#define LOG_TAG "android.hardware.camera.device@3.2-service.stm32mp1"
// #define LOG_NDEBUG 0

#include <utils/Log.h>

#include "camera_device.h"

#include "camera_device_session.h"
#include "CameraMetadata.h"
#include "metadata_factory.h"

namespace android {
namespace hardware {
namespace camera {
namespace device {
namespace V3_2 {
namespace implementation {

using CameraMetadataHelper =
              ::android::hardware::camera::common::V1_0::helper::CameraMetadata;

CameraDevice::CameraDevice(const std::string& node)
  : camera_node_ { node },
    init_fail_ { false },
    disconnected_ { false },
    session_ { nullptr },
    v4l2_wrapper_ { new V4L2Wrapper(node) },
    metadata_ { nullptr },
    static_info_ { nullptr },
    lock_ { } {
}

int CameraDevice::initialize() {
  /* Get Camera Characteristics */
  std::unique_ptr<Metadata> metadata;
  int res = MetadataFactory::GetV4L2Metadata(v4l2_wrapper_, &metadata);

  if (res) {
    ALOGE("%s: failed to initialize V4L2 metadata: %d", __FUNCTION__, res);
    return res;
  }

  metadata_ = std::move(metadata);

  /* Get static metadata */
  std::unique_ptr<CameraMetadataHelper> out =
                                      std::make_unique<CameraMetadataHelper>();

  res = metadata_->FillStaticMetadata(out.get());
  if (res) {
    ALOGE("%s: failed to get static metadata: %d", __FUNCTION__, res);
    return res;
  }

  /* Wrap static metadata into StaticProperties */
  static_info_.reset(StaticProperties::NewStaticProperties(std::move(out)));
  if (!static_info_) {
    ALOGE("%s: failed to initialize static properties from device metadata: %d",
                                                            __FUNCTION__, res);
    return res;
  }

  return 0;
}

Status CameraDevice::initStatus() const {
    Mutex::Autolock _l(lock_);

    if (init_fail_) {
        return Status::INTERNAL_ERROR;
    }
    if (disconnected_) {
        return Status::CAMERA_DISCONNECTED;
    }

    return Status::OK;
}

// Methods from ::android::hardware::camera::device::V3_2::ICameraDevice follow.
Return<void> CameraDevice::getResourceCost(getResourceCost_cb _hidl_cb) {
    Status status = initStatus();
    CameraResourceCost res_cost;

    if (status == Status::OK) {
        std::vector<std::string> conflicting_devices;
        int cost = 100;

        res_cost.resourceCost = cost;
        res_cost.conflictingDevices.resize(conflicting_devices.size());

        for (size_t i = 0; i < conflicting_devices.size(); ++i) {
            res_cost.conflictingDevices[i] = conflicting_devices[i];
            ALOGV("CameraDevice %s is conflicting with CameraDevice %s",
                    camera_node_.c_str(), res_cost.conflictingDevices[i].c_str());
        }
    }

    _hidl_cb(status, res_cost);

    return Void();
}

Return<void> CameraDevice::getCameraCharacteristics(
        getCameraCharacteristics_cb _hidl_cb) {
    Status status = initStatus();
    CameraMetadata camera_characteristics;

    if (status == Status::OK) {
      const camera_metadata_t* raw_metadata = static_info_->raw_metadata();

      camera_characteristics.setToExternal(
          const_cast<uint8_t *>(reinterpret_cast<const uint8_t *>(raw_metadata)),
          get_camera_metadata_size(raw_metadata));
    }

    _hidl_cb(status, camera_characteristics);

    return Void();
}

Return<Status> CameraDevice::setTorchMode(TorchMode /* mode */) {
    Status status = initStatus();

    if (status == Status::OK) {
        status = Status::OPERATION_NOT_SUPPORTED;
    }

    return status;
}

Return<void> CameraDevice::open(const sp<ICameraDeviceCallback>& callback,
        open_cb _hidl_cb) {
    Status status = initStatus();
    sp<CameraDeviceSession> session = nullptr;

    if (callback == nullptr) {
        ALOGE("%s: cannot open camera %s. callback is null!",
                __FUNCTION__, camera_node_.c_str());
        status = Status::ILLEGAL_ARGUMENT;
        goto open_out;
    }

    if (status == Status::OK) {
        Mutex::Autolock _l(lock_);

        ALOGV("%s: initializing device for camera %s",
                __FUNCTION__, camera_node_.c_str());

        session = session_.promote();

        if (session != nullptr && !session->isClosed()) {
            ALOGE("%s: cannot open an already opened camera!", __FUNCTION__);
            status = Status::CAMERA_IN_USE;
            goto open_out;
        }

        session = this->createSession(callback);

        if (session == nullptr) {
            ALOGE("%s: camera device session allocation failed", __FUNCTION__);
            status = Status::INTERNAL_ERROR;
            goto open_out;
        }

        int res = session->initialize();

        if (res) {
          ALOGE("%s: failed to initialize camera device session: %d",
                                                            __FUNCTION__, res);
          status = Status::INTERNAL_ERROR;
          goto open_out;
        }

/*
        if (session->isInitFailed()) {
          // TODO
        }
*/
        session_ = session;
    }

open_out:
    _hidl_cb(status, session);

    return Void();
}

sp<CameraDeviceSession>
CameraDevice::createSession(const sp<ICameraDeviceCallback>& callback) {

  sp<CameraDeviceSession> session = new CameraDeviceSession(v4l2_wrapper_,
                                                            metadata_,
                                                            static_info_,
                                                            callback);
  return session;
}

Return<void> CameraDevice::dumpState(const hidl_handle& handle) {
    Mutex::Autolock _l(lock_);
    sp<CameraDeviceSession> session = nullptr;
    int fd = -1;

    if (handle.getNativeHandle() == nullptr) {
        ALOGE("%s: handle must not be null", __FUNCTION__);
        return Void();
    }
    if (handle->numFds != 1 || handle->numInts != 0) {
        ALOGE("%s: handle must contain 1 FD and 0 ints! Got %d FDs and %d ints",
                __FUNCTION__, handle->numFds, handle->numInts);
        return Void();
    }

    fd = handle->data[0];
    session = session_.promote();

    if (session == nullptr) {
        dprintf(fd, "No active camera device session instance\n");
        return Void();
    }

    session->dumpState(handle);
    return Void();
}

}  // namespace implementation
}  // namespace V3_2
}  // namespace device
}  // namespace camera
}  // namespace hardware
}  // namespace android
