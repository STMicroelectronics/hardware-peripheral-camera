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

#define LOG_TAG "android.hardware.camera.common@1.0-helper.stm32mp1"
// #define LOG_NDEBUG 0

#include <utils/Log.h>

#include "mapper_helper.h"

namespace android {
namespace hardware {
namespace camera {
namespace common {
namespace V1_0 {
namespace helper {

using MapperError = android::hardware::graphics::mapper::V2_0::Error;

MapperHelper::MapperHelper() : initialized__(false) {}

void MapperHelper::initializeLocked() {
  if (initialized__) {
      return;
  }

  mapper_ = IMapper::getService();
  if (mapper_ == nullptr) {
    ALOGE("%s: cannnot acccess graphics mapper HAL!", __FUNCTION__);
    return;
  }

  initialized__ = true;
  return;
}

void MapperHelper::cleanup() {
  mapper_.clear();
  initialized__ = false;
}

// In IComposer, any buffer_handle_t is owned by the caller and we need to
// make a clone for hwcomposer2.  We also need to translate empty handle
// to nullptr.  This function does that, in-place.
bool MapperHelper::importBuffer(buffer_handle_t& handle) {
  if (!handle->numFds && !handle->numInts) {
    handle = nullptr;
    return true;
  }

  Mutex::Autolock lock(mLock);
  if (!initialized__) {
     initializeLocked();
  }

  if (mapper_ == nullptr) {
      ALOGE("%s: mapper_ is null!", __FUNCTION__);
      return false;
  }

  MapperError error;
  buffer_handle_t imported_handle;

  auto ret = mapper_->importBuffer(hidl_handle(handle),
            [&](const auto& tmp_error, const auto& tmp_buffer_handle) {
              error = tmp_error;
              imported_handle = static_cast<buffer_handle_t>(tmp_buffer_handle);
            });

  if (!ret.isOk()) {
    ALOGE("%s: mapper importBuffer failed: %s",
                                      __FUNCTION__, ret.description().c_str());
    return false;
  }

  if (error != MapperError::NONE) {
    return false;
  }

  handle = imported_handle;

  return true;
}

void MapperHelper::freeBuffer(buffer_handle_t handle) {
  if (!handle) {
      return;
  }

  Mutex::Autolock lock(mLock);

  if (mapper_ == nullptr) {
      ALOGE("%s: mapper_ is null!", __FUNCTION__);
      return;
  }

  auto ret = mapper_->freeBuffer(const_cast<native_handle_t*>(handle));

  if (!ret.isOk()) {
      ALOGE("%s: mapper freeBuffer failed: %s",
                                      __FUNCTION__, ret.description().c_str());
  }
}

bool MapperHelper::importFence(const native_handle_t* handle, int& fd) const {
  if (handle == nullptr || handle->numFds == 0) {
    fd = -1;
  } else if (handle->numFds == 1) {
    fd = dup(handle->data[0]);
    if (fd < 0) {
      ALOGE("failed to dup fence fd %d", handle->data[0]);
      return false;
    }
  } else {
    ALOGE("invalid fence handle with %d file descriptors",  handle->numFds);
    return false;
  }

  return true;
}

void MapperHelper::closeFence(int fd) const {
  if (fd >= 0) {
    close(fd);
  }
}

void* MapperHelper::lock(buffer_handle_t& buf, uint64_t usage, uint32_t width,
                         uint32_t height, hidl_handle& acquire_fence) {
  Mutex::Autolock lock(mLock);
  IMapper::Rect region { 0, 0, static_cast<int>(width),
                                                    static_cast<int>(height) };
  void *ret = 0;

  if (!initialized__) {
      initializeLocked();
  }

  if (mapper_ == nullptr) {
      ALOGE("%s: mapper_ is null!", __FUNCTION__);
      return ret;
  }

  auto buffer = const_cast<native_handle_t *>(buf);

  mapper_->lock(buffer, usage, region, acquire_fence,
                [&](const auto& tmp_error, const auto& tmp_ptr) {
                  if (tmp_error == MapperError::NONE) {
                    ret = tmp_ptr;
                  } else {
                    ALOGE("%s: failed to lock error %d!",
                                                       __FUNCTION__, tmp_error);
                  }
                });

  return ret;
}


YCbCrLayout MapperHelper::lockYCbCr(buffer_handle_t& buf, uint64_t usage,
                                    uint32_t width, uint32_t height,
                                    hidl_handle& acquire_fence) {
  Mutex::Autolock lock(mLock);
  IMapper::Rect region { 0, 0, static_cast<int>(width),
                                                    static_cast<int>(height) };
  YCbCrLayout layout = {};

  if (!initialized__) {
      initializeLocked();
  }

  if (mapper_ == nullptr) {
      ALOGE("%s: mapper_ is null!", __FUNCTION__);
      return layout;
  }

  auto buffer = const_cast<native_handle_t *>(buf);

  mapper_->lockYCbCr(buffer, usage, region, acquire_fence,
                     [&](const auto& tmp_error, const auto& tmp_layout) {
                       if (tmp_error == MapperError::NONE) {
                         layout = tmp_layout;
                       } else {
                         ALOGE("%s: failed to lockYCbCr error %d!",
                                                      __FUNCTION__, tmp_error);
                       }
                     });

  return layout;
}

int MapperHelper::unlock(buffer_handle_t& buf, hidl_handle *release_fence) {
  int res = 0;
  auto buffer = const_cast<native_handle_t*>(buf);

  mapper_->unlock(buffer,
                  [&](const auto& tmp_error, const auto& tmp_release_fence) {
                    if (tmp_error == MapperError::NONE) {
                      if (release_fence) {
                        *release_fence = tmp_release_fence;
                      }
                    } else {
                      ALOGE("%s: failed to unlock error %d!",
                                                      __FUNCTION__, tmp_error);
                      res = static_cast<int>(tmp_error);
                    }
                  });

    return res;
}

} // namespace helper
} // namespace V1_0
} // namespace common
} // namespace camera
} // namespace hardware
} // namespace android
