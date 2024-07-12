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

#ifndef CAMERA_COMMON_1_0_HANDLEIMPORTED_H
#define CAMERA_COMMON_1_0_HANDLEIMPORTED_H

#include <android/hardware/graphics/mapper/2.0/IMapper.h>
#include <cutils/native_handle.h>

#include <mutex>

using android::hardware::graphics::mapper::V2_0::IMapper;
using android::hardware::graphics::mapper::V2_0::YCbCrLayout;

namespace android {
namespace hardware {
namespace camera {
namespace common {
namespace V1_0 {
namespace helper {

// Borrowed from graphics HAL. Use this until gralloc mapper HAL is working
class MapperHelper {
public:
    MapperHelper();

    // In IComposer, any buffer_handle_t is owned by the caller and we need to
    // make a clone for hwcomposer2.  We also need to translate empty handle
    // to nullptr.  This function does that, in-place.
    bool importBuffer(buffer_handle_t& handle);
    void freeBuffer(buffer_handle_t handle);
    bool importFence(const native_handle_t* handle, int& fd) const;
    void closeFence(int fd) const;

    void* lock(buffer_handle_t& buf, uint64_t usage, uint32_t width,
               uint32_t height, hidl_handle& acquire_fence);

    // Assume caller has done waiting for acquire fences
    YCbCrLayout lockYCbCr(buffer_handle_t& buf, uint64_t usage, uint32_t width,
                          uint32_t height, hidl_handle& acquire_fence);

    int unlock(buffer_handle_t& buf, hidl_handle *release_fence);

private:
    void initializeLocked();
    void cleanup();

    std::mutex mLock;
    bool initialized__;
    sp<IMapper> mapper_;

};

} // namespace helper
} // namespace V1_0
} // namespace common
} // namespace camera
} // namespace hardware
} // namespace android

#endif // CAMERA_COMMON_1_0_HANDLEIMPORTED_H
