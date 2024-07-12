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

#ifndef AIDL_ANDROID_HARDWARE_CAMERA_DEVICE_V4L2_CAMERA_DEVICE_SESSION_H
#define AIDL_ANDROID_HARDWARE_CAMERA_DEVICE_V4L2_CAMERA_DEVICE_SESSION_H

#include <aidl/android/hardware/camera/common/Status.h>
#include <aidl/android/hardware/camera/device/BnCameraDeviceSession.h>
#include <aidl/android/hardware/camera/device/ICameraDeviceCallback.h>

#include <fmq/AidlMessageQueue.h>
#include <cutils/properties.h>

#include <mutex>
#include <unordered_map>

#include <metadata/metadata.h>

#include "v4l2_camera_config.h"
#include "v4l2_stream.h"
#include "static_properties.h"

namespace android {
namespace hardware {
namespace camera {
namespace device {
namespace implementation {

using namespace ::android::hardware::camera::common::V1_0;

using ::android::hardware::camera::common::V1_0::metadata::Metadata;
using ::android::hardware::camera::common::V1_0::metadata::MetadataCommon;

using aidl::android::hardware::common::fmq::MQDescriptor;
using aidl::android::hardware::common::fmq::SynchronizedReadWrite;

using aidl::android::hardware::camera::common::Status;
using aidl::android::hardware::camera::device::BnCameraDeviceSession;
using aidl::android::hardware::camera::device::BufferCache;
using aidl::android::hardware::camera::device::ErrorCode;
using aidl::android::hardware::camera::device::CameraMetadata;
using aidl::android::hardware::camera::device::CameraOfflineSessionInfo;
using aidl::android::hardware::camera::device::CaptureRequest;
using aidl::android::hardware::camera::device::HalStream;
using aidl::android::hardware::camera::device::ICameraDeviceCallback;
using aidl::android::hardware::camera::device::ICameraOfflineSession;
using aidl::android::hardware::camera::device::RequestTemplate;
using aidl::android::hardware::camera::device::StreamConfiguration;

using MetadataQueue = AidlMessageQueue<int8_t, SynchronizedReadWrite>;
using CameraMetadaPtr = std::shared_ptr<const helper::CameraMetadata>;

using ndk::ScopedAStatus;

class V4l2CameraDeviceSession : public BnCameraDeviceSession,
                                public V4l2Stream::CallbackInterface {
public:
  static std::shared_ptr<V4l2CameraDeviceSession> Create(
      const V4l2CameraConfig &config,
      std::shared_ptr<Metadata> metadata,
      std::shared_ptr<StaticProperties> static_info,
      const std::shared_ptr<ICameraDeviceCallback> &callback);

  V4l2CameraDeviceSession(
      const V4l2CameraConfig &config,
      std::shared_ptr<Metadata> metadata,
      std::shared_ptr<StaticProperties> static_info,
      const std::shared_ptr<ICameraDeviceCallback> &callback);
  virtual ~V4l2CameraDeviceSession();

  // Override functions in ICameraDevice
  ScopedAStatus close() override;

  ScopedAStatus configureStreams(const StreamConfiguration &config,
                                 std::vector<HalStream> *result) override;

  ScopedAStatus constructDefaultRequestSettings(
      RequestTemplate type, CameraMetadata *settings) override;

  ScopedAStatus flush() override;

  ScopedAStatus getCaptureRequestMetadataQueue(
      MQDescriptor<int8_t, SynchronizedReadWrite> *queue) override;

  ScopedAStatus getCaptureResultMetadataQueue(
      MQDescriptor<int8_t, SynchronizedReadWrite> *queue) override;

  ScopedAStatus isReconfigurationRequired(
      const CameraMetadata &old_session_params,
      const CameraMetadata &new_session_params, bool *required) override;

  ScopedAStatus processCaptureRequest(
      const std::vector<CaptureRequest> &requests,
      const std::vector<BufferCache> &caches_to_remove,
      int32_t *result) override;

  ScopedAStatus signalStreamFlush(const std::vector<int32_t> &stream_ids,
                                  int32_t stream_config_counter) override;

  ScopedAStatus switchToOffline(
      const std::vector<int32_t> &streams_to_keep,
      CameraOfflineSessionInfo *offline_session_info,
      std::shared_ptr<ICameraOfflineSession> *session) override;

  ScopedAStatus repeatingRequestEnd(
      int32_t /* frame_number */,
      const std::vector<int32_t>& /* stream_idsi */) override {
    return ScopedAStatus::ok();
  };

  // End of override functions in ICameraDevice

  // Override function in V4l2Stream::CallbackInterface

  void processCaptureBufferError(const V4l2Stream::TrackedStreamBuffer &tsb);
  void processCaptureBufferResult(const V4l2Stream::TrackedStreamBuffer &tsb);

private:
  Status initialize();
  Status initStatus();

  Status configureStreamsVerification(
      const StreamConfiguration &requested_configuration);
  Status configureStreamsClean(
      const StreamConfiguration &requested_configuration);
  Status configureDriverStreams(
      const StreamConfiguration &requested_configuration);
  Status configureStreamsResult(
      const StreamConfiguration &requested_configuration,
      std::vector<HalStream> *result);

  Status findBestStreamConfiguration(
      const Stream &stream, V4l2StreamConfig &res);

  void updateBufferCaches(const std::vector<BufferCache> &caches_to_remove);
  Status processOneCaptureRequest(const CaptureRequest &request);
  Status processCaptureRequestVerification(const CaptureRequest &request);
  Status processCaptureRequestMetadata(
      const CaptureRequest &request,
      std::shared_ptr<helper::CameraMetadata> &metadata);
  Status processCaptureRequestEnqueue(
      const CaptureRequest &request,
      const std::shared_ptr<const helper::CameraMetadata> &settings);

  void processCaptureMetadataResult(
      int32_t frame_number, const helper::CameraMetadata &settings);

  void processCaptureRequestError(const CaptureRequest &request, ErrorCode e);

  void ISPThread();

private:
  V4l2CameraConfig config_;
  std::shared_ptr<ICameraDeviceCallback> callback_;

  std::shared_ptr<Metadata> metadata_;
  std::shared_ptr<StaticProperties> static_info_;
  std::vector<uint8_t> previous_settings_;
  std::unique_ptr<MetadataQueue> request_metadata_queue_;
  std::unique_ptr<MetadataQueue> result_metadata_queue_;
  std::unique_ptr<const CameraMetadataHelper> default_settings_[
                                        MetadataCommon::kRequestTemplateCount];

  std::unordered_map<uint32_t, std::shared_ptr<V4l2Stream>> stream_map_;

  std::unique_ptr<std::thread> isp_thread_;
  std::condition_variable isp_cond_;
  std::mutex isp_mutex_;

  std::mutex flush_mutex_;
  std::mutex result_mutex_;

  bool closed_;
};

} // implementation
} // device
} // camera
} // hardware
} // android

#endif // AIDL_ANDROID_HARDWARE_CAMERA_DEVICE_V4L2_CAMERA_DEVICE_SESSION_H
