/*
 * Copyright 2021, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#define LOG_TAG "android.hardware.camera.provider@1.1-service"

#include <aidl/android/hardware/camera/provider/ICameraProvider.h>

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <binder/ProcessState.h>

#include "v4l2_camera_provider.h"

using ::aidl::android::hardware::camera::provider::ICameraProvider;

using ::android::hardware::camera::provider::implementation::V4l2CameraProvider;

const std::string kProviderInstance = "/internal/0";

int main() {
    ALOGI("ST camera provider service is starting.");

    std::shared_ptr<ICameraProvider> provider = V4l2CameraProvider::Create();
    if (provider == nullptr)
        return android::NO_INIT;

    const std::string instance = std::string() +
                                 V4l2CameraProvider::descriptor +
                                 kProviderInstance;

    binder_status_t status =
        AServiceManager_addService(provider->asBinder().get(),
                                   instance.c_str());
    if (status != STATUS_OK) {
        ALOGE("Cannot register AIDL ST camera provider service: %d !", status);
        return android::NO_INIT;
    }

    // Thread pool for vendor libbinder for internal vendor services
    android::ProcessState::self()->setThreadPoolMaxThreadCount(6);
    android::ProcessState::self()->startThreadPool();

    // Thread pool for system libbinder (via libbinder_ndk) for aidl services
    // IComposer and IDisplay
    ABinderProcess_setThreadPoolMaxThreadCount(6);
    ABinderProcess_startThreadPool();
    ABinderProcess_joinThreadPool();

    return EXIT_FAILURE;  // should not reach
}
