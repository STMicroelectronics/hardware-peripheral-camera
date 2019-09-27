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

#define LOG_TAG "android.hardware.camera.provider@2.4-service.stm32mp1"
// #define LOG_NDEBUG 0

#include <utils/Log.h>

#include "camera_provider.h"

#include <ctype.h>
#include <dirent.h>
#include <string.h>
#include <sys/types.h>
#include <unordered_set>

#include "camera_device.h"

namespace android {
namespace hardware {
namespace camera {
namespace provider {
namespace V2_4 {
namespace implementation {

const int kMaxCameraDeviceNameLen = 128;

using ::android::hardware::camera::common::V1_0::Status;
using ::android::hardware::camera::common::V1_0::v4l2::V4L2Wrapper;

CameraProvider::CameraProvider()
  : callback_(nullptr),
    vendor_tag_sections_() {}

std::string CameraProvider::getHidlDeviceName(std::string node) {
  char device_name[kMaxCameraDeviceNameLen];
  int version_major = 3;
  int version_minor = 2;

  // TODO: review version

  std::string camera_id = node.substr(kVideoDev.size());

  snprintf(device_name, sizeof(device_name), "device@%d.%d/v4l2/%s",
              version_major, version_minor, camera_id.c_str());

  return device_name;
}

void CameraProvider::initialize() {
  DIR *dir = opendir("/dev");

  if (dir == nullptr) {
    ALOGE("%s: failed to open '/dev' (%s)", __FUNCTION__, strerror(errno));
    return;
  }

  int fd = -1;
  dirent *ent = nullptr;
  v4l2_capability cap;
  std::unordered_set<std::string> buses;

  while ((ent = readdir(dir))) {
    std::string node = std::string("/dev/") + ent->d_name;

    if (kVideoDev.compare(0, kVideoDev.size(), node, 0, kVideoDev.size()) == 0) {
      if (node.size() > kVideoDev.size() && isdigit(node[kVideoDev.size()])) {
        ALOGV("%s: found video node %s", __FUNCTION__, node.c_str());

        fd = TEMP_FAILURE_RETRY(open(node.c_str(), O_RDWR));
        if (fd < 0) {
          ALOGE("%s: failed to open %s (%s)",
                    __FUNCTION__, node.c_str(), strerror(errno));
          continue;
        }
        if (V4L2Wrapper::IsV4L2VideoDevice(fd, &cap) == 0) {
          if (buses.insert(reinterpret_cast<char*>(cap.bus_info)).second) {
            ALOGV("%s: found unique bus at %s", __FUNCTION__, node.c_str());

            std::string name = getHidlDeviceName(node);
            camera_nodes_[name] = node;
          }
        }

        close(fd);
      }
    }
  }
}

// Methods from ::android::hardware::camera::provider::V2_4::ICameraProvider follow.
Return<Status> CameraProvider::setCallback(
        const sp<ICameraProviderCallback>& callback) {
  callback_ = callback;

  return Status::OK;
}

Return<void> CameraProvider::getVendorTags(getVendorTags_cb _hidl_cb) {
  _hidl_cb(Status::OK, vendor_tag_sections_);

  return Void();
}

Return<void> CameraProvider::getCameraIdList(getCameraIdList_cb _hidl_cb) {
  std::vector<hidl_string> list;

  for (auto const& node_name_pair : camera_nodes_) {
    list.push_back(node_name_pair.first);
    ALOGE("%s: camera id: %s", __func__, node_name_pair.first.c_str());
  }

  hidl_vec<hidl_string> hidlDeviceNameList(list);

  _hidl_cb(Status::OK, hidlDeviceNameList);

  return Void();
}

Return<void> CameraProvider::isSetTorchModeSupported(
        isSetTorchModeSupported_cb _hidl_cb) {
  _hidl_cb(Status::OK, true);

  return Void();
}

Return<void> CameraProvider::getCameraDeviceInterface_V1_x(
        const hidl_string& camera_device_name,
        getCameraDeviceInterface_V1_x_cb _hidl_cb) {
  std::string device_name(camera_device_name);
  ALOGE("%s: camera device %s does not support version 1.0!",
                __FUNCTION__, device_name.c_str());
  _hidl_cb(Status::OPERATION_NOT_SUPPORTED, nullptr);

  return Void();
}

Return<void> CameraProvider::getCameraDeviceInterface_V3_x(
        const hidl_string& camera_device_name,
        getCameraDeviceInterface_V3_x_cb _hidl_cb) {
  Status status = Status::OK;
  std::string device_name(camera_device_name);
  sp<device::V3_2::implementation::CameraDevice> camera_device = nullptr;
  auto it = camera_nodes_.find(camera_device_name);

  if (it == camera_nodes_.end()) {
    ALOGE("%s: cannot find camera %s!", __FUNCTION__, device_name.c_str());
    status = Status::ILLEGAL_ARGUMENT;
  } else {
    camera_device = new device::V3_2::implementation::CameraDevice(it->second);
    camera_device->initialize();
  }

  _hidl_cb(Status::OK, camera_device);

  return Void();
}

}  // namespace implementation
}  // namespace V2_4
}  // namespace provider
}  // namespace camera
}  // namespace hardware
}  // namespace android
