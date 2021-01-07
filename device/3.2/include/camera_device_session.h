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

#ifndef ANDROID_HARDWARE_CAMERA_DEVICE_V3_2_CAMERADEVICESESSION_H
#define ANDROID_HARDWARE_CAMERA_DEVICE_V3_2_CAMERADEVICESESSION_H

#include <android/hardware/camera/device/3.2/ICameraDeviceSession.h>
#include <android/hardware/camera/device/3.2/ICameraDeviceCallback.h>

#include <thread>
#include <unordered_map>

#include <arc/frame_buffer.h>
#include <camera_tracker.h>
#include <fmq/MessageQueue.h>
#include <hidl/MQDescriptor.h>
#include <hidl/Status.h>
#include <helper/mapper_helper.h>
#include <metadata/metadata.h>
#include <metadata/metadata_common.h>
#include <static_properties.h>
#include <utils/Condition.h>
#include <utils/Mutex.h>
#include <v4l2/v4l2_wrapper.h>

namespace android {
namespace hardware {
namespace camera {
namespace device {
namespace V3_2 {
namespace implementation {

using namespace ::android::hardware::camera::common::V1_0;

using ::android::hardware::camera::common::V1_0::helper::MapperHelper;
using ::android::hardware::camera::common::V1_0::v4l2::StreamFormat;
using ::android::hardware::camera::common::V1_0::v4l2::StreamFormats;
using ::android::hardware::camera::common::V1_0::v4l2::V4L2Wrapper;
using ::android::hardware::camera::common::V1_0::Status;

using CameraMetadataHelper =
        ::android::hardware::camera::common::V1_0::helper::CameraMetadata;

using ::android::hardware::camera::common::V1_0::metadata::Metadata;
using ::android::hardware::camera::common::V1_0::metadata::MetadataCommon;

using ::android::hardware::kSynchronizedReadWrite;
using ::android::hardware::hidl_array;
using ::android::hardware::hidl_memory;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::sp;

#define MAX_BUFFER_NUMBER 3

struct CameraDeviceSession : public ICameraDeviceSession {
public:
  CameraDeviceSession(std::shared_ptr<V4L2Wrapper> v4l2_wrapper,
                      std::shared_ptr<Metadata> metadata,
                      std::shared_ptr<StaticProperties> static_info,
                      const sp<ICameraDeviceCallback>& callback);

  int initialize();

  Return<void> constructDefaultRequestSettings(
                        RequestTemplate type,
                        constructDefaultRequestSettings_cb _hidl_cb) override;
  Return<void> configureStreams(
                        const StreamConfiguration& requestedConfiguration,
                        configureStreams_cb _hidl_cb) override;
  Return<void> processCaptureRequest(
                        const hidl_vec<CaptureRequest>& requests,
                        const hidl_vec<BufferCache>& cachesToRemove,
                        processCaptureRequest_cb _hidl_cb) override;
  Return<void> getCaptureRequestMetadataQueue(
                        getCaptureRequestMetadataQueue_cb _hidl_cb) override;
  Return<void> getCaptureResultMetadataQueue(
                        getCaptureResultMetadataQueue_cb _hidl_cb) override;
  Return<::android::hardware::camera::common::V1_0::Status> flush() override;
  Return<void> close() override;

  bool isClosed() const;
  void dumpState(const native_handle_t* fd) const;

protected:
  Status initStatus() const;

  void updateBufferCaches(const hidl_vec<BufferCache>& cache_to_remove);

  /* method to configure streams */
  Status configureStreamsVerification(
                            const StreamConfiguration& requested_configuration);
  Status configureStreamClean(
                            const StreamConfiguration& requested_configuration);
  Status configureDriverStream(
                            const StreamConfiguration& requested_configuration);
  Status findBestFitFormat(const StreamConfiguration& requested_configuration,
                           StreamFormat *res);
  Status configureStreamResult(
                            const StreamConfiguration& requestedConfiguration,
                            HalStreamConfiguration *configuration);

  /* method to process capture requests */
  Status processOneCaptureRequest(const CaptureRequest& request);
  Status processCaptureRequestVerification(const CaptureRequest& request);
  Status processCaptureRequestMetadata(const CaptureRequest& request,
                            std::shared_ptr<helper::CameraMetadata>& settings);
  Status processCaptureRequestImport(const CaptureRequest& request);
  Status processCaptureRequestEnqueue(const CaptureRequest& request,
                const std::shared_ptr<const helper::CameraMetadata>& settings);
  Status processCaptureRequestError(const CaptureRequest& request,
                                    ErrorCode error);
  Status processCaptureBufferError(uint32_t capture_id,
                                   StreamBuffer buffer, ErrorCode error);

  /* method to process capture results */
  void captureRequestThread();
  Status processCaptureResultConversion(
                      const std::unique_ptr<arc::V4L2FrameBuffer>& v4l2_buffer,
                      StreamBuffer& stream_buffer,
                      std::shared_ptr<const helper::CameraMetadata>& settings);
  Status processCaptureResultMetadata(const helper::CameraMetadata& settings,
                                      CaptureResult *result);

  Status processCaptureResult(uint32_t capture_id, StreamBuffer stream_buffer);
  void processCaptureRequestError(const CaptureRequest& request);

  /* utility */
  Status saveBuffer(const StreamBuffer& stream_buffer);
  buffer_handle_t getSavedBuffer(const StreamBuffer& stream_buffer);

protected:
  bool closed_;
  bool disconnected_;
  bool init_fail_;
  bool started_;
  mutable Mutex state_lock_;
  mutable Mutex flush_queue_lock_;
  mutable Mutex flush_result_lock_;
  const sp<ICameraDeviceCallback> callback_;
  CameraMetadata previous_settings_;
  std::shared_ptr<Metadata> metadata_;
  std::shared_ptr<StaticProperties> static_info_;

  std::vector<std::unique_ptr<arc::V4L2FrameBuffer>> v4l2_buffers_;

  /* V4L2 Wrapper*/
  std::shared_ptr<V4L2Wrapper> v4l2_wrapper_;
  std::shared_ptr<V4L2Wrapper::Connection> connection_;

  /* All formats supported by the driver */
  StreamFormats supported_formats_;
  /* Supported driver formats from which we can convert to another one */
  StreamFormats qualified_formats_;
  uint32_t implem_defined_format_;

  /* capture tracker  */
  CameraTracker camera_tracker_;
  mutable Mutex capture_tracker_lock_;
  mutable Mutex capture_wait_lock_;
  Condition capture_active_;

  /* capture variable */
  std::unique_ptr<std::thread> capture_result_thread_;

  /* MessageQueue used by the framework to give CameraMetadata to the HAL
   * Used by:
   *   - getCaptureRequestMetadataQueue  (the framework get the queue and fill it)
   *   - processCaptureRequest[Metadata] (the HAL read from the filled queue)
   *
   *   - processCaptureResult[Metadata]  (the HAL write metadata to the queue)
   *   - getCaptureResultMetadataQueue   (the framework get the queue and read it)
   */
  using RequestMetadataQueue = MessageQueue<uint8_t, kSynchronizedReadWrite>;
  std::unique_ptr<RequestMetadataQueue> request_metadata_queue_;
  using ResultMetadataQueue = MessageQueue<uint8_t, kSynchronizedReadWrite>;
  std::unique_ptr<ResultMetadataQueue> result_metadata_queue_;

  std::unique_ptr<const CameraMetadataHelper> default_settings_[
                                        MetadataCommon::kRequestTemplateCount];

  static MapperHelper mapper_helper_;

};

}  // namespace implementation
}  // namespace V3_2
}  // namespace device
}  // namespace camera
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_CAMERA_DEVICE_V3_4_CAMERADEVICESESSION_H
