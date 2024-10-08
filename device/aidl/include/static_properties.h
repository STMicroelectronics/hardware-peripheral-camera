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

#ifndef DEFAULT_CAMERA_HAL_STATIC_PROPERTIES_H_
#define DEFAULT_CAMERA_HAL_STATIC_PROPERTIES_H_

#include <aidl/android/hardware/camera/device/StreamConfiguration.h>
#include <aidl/android/hardware/camera/device/RequestTemplate.h>
#include <aidl/android/hardware/camera/device/Stream.h>
#include <aidl/android/hardware/graphics/common/PixelFormat.h>

#include <memory>
#include <set>

#include <CameraMetadata.h>
#include "metadata/metadata_reader.h"
#include "metadata/types.h"

namespace android {
namespace hardware {
namespace camera {
namespace device {
namespace implementation {

using aidl::android::hardware::camera::device::StreamConfiguration;
using aidl::android::hardware::camera::device::RequestTemplate;
using aidl::android::hardware::camera::device::Stream;
using aidl::android::hardware::graphics::common::PixelFormat;

using ::android::hardware::camera::common::V1_0::metadata::MetadataReader;
using ::android::hardware::camera::common::V1_0::metadata::ReprocessFormatMap;
using ::android::hardware::camera::common::V1_0::metadata::StreamSpec;

using CameraMetadataHelper =
              ::android::hardware::camera::common::V1_0::helper::CameraMetadata;

// StaticProperties provides a wrapper around useful static metadata entries.
class StaticProperties {
 public:
  // Helpful types for interpreting some static properties.
  struct StreamCapabilities {
    int64_t stall_duration;
    int32_t input_supported;
    int32_t output_supported;
    // Default constructor ensures no support
    // and an invalid stall duration.
    StreamCapabilities()
        : stall_duration(-1), input_supported(0), output_supported(0) {}
  };
  // Map stream spec (format, size) to their
  // capabilities (input, output, stall).
  typedef std::map<StreamSpec, StreamCapabilities, StreamSpec::Compare>
      CapabilitiesMap;

  // Use this method to create StaticProperties objects.
  // Functionally equivalent to "new StaticProperties",
  // except that it may return nullptr in case of failure (missing entries).
  static StaticProperties* NewStaticProperties(
                        std::unique_ptr<const MetadataReader> metadata_reader);
  static StaticProperties* NewStaticProperties(
                            std::unique_ptr<CameraMetadataHelper> metadata) {
    return NewStaticProperties(
                        std::make_unique<MetadataReader>(std::move(metadata)));
  }
  virtual ~StaticProperties(){};

  // Simple accessors.
  int facing() const { return facing_; };
  int orientation() const { return orientation_; };
  // Carrying on the promise of the underlying reader,
  // the returned pointer is valid only as long as this object is alive.
  const camera_metadata_t* raw_metadata() const {
    return metadata_reader_->raw_metadata();
  };

  // Check if a given template type is supported.
  bool TemplateSupported(RequestTemplate type) const;
  // Validators (check that values are consistent with the capabilities
  // this object represents/base requirements of the camera HAL).
  bool StreamConfigurationSupported(const StreamConfiguration* stream_config);
  // Check that the inputs and outputs for a request don't conflict.
  bool ReprocessingSupported(const Stream* input_stream,
                             const std::set<const Stream*>& output_streams);

 private:
  // Constructor private to allow failing on bad input.
  // Use NewStaticProperties instead.
  StaticProperties(std::unique_ptr<const MetadataReader> metadata_reader,
                   int facing,
                   int orientation,
                   const std::vector<int64_t> &available_use_cases,
                   const std::vector<uint8_t> &available_rotations,
                   int32_t max_input_streams,
                   int32_t max_raw_output_streams,
                   int32_t max_non_stalling_output_streams,
                   int32_t max_stalling_output_streams,
                   std::set<uint8_t> request_capabilities,
                   CapabilitiesMap stream_capabilities,
                   ReprocessFormatMap supported_reprocess_outputs);

  // Helper functions for StreamConfigurationSupported.
  bool SanityCheckStreamConfiguration(
      const StreamConfiguration* stream_config);
  bool InputStreamsSupported(
      const StreamConfiguration* stream_config);
  bool OutputStreamsSupported(
      const StreamConfiguration* stream_config);
  bool OperationModeSupported(
      const StreamConfiguration* stream_config);

  const std::unique_ptr<const MetadataReader> metadata_reader_;
  const int facing_;
  const int orientation_;
  const std::vector<int64_t> available_use_cases_;
  const std::vector<uint8_t> available_rotations_;
  const int32_t max_input_streams_;
  const int32_t max_raw_output_streams_;
  const int32_t max_non_stalling_output_streams_;
  const int32_t max_stalling_output_streams_;
  const std::set<uint8_t> request_capabilities_;
  const CapabilitiesMap stream_capabilities_;
  const ReprocessFormatMap supported_reprocess_outputs_;

  StaticProperties(const StaticProperties&);
  void operator=(const StaticProperties&);
};

} // namespace implementation
} // namespace device
} // namespace camera
} // namespace hardware
} // namespace android

#endif  // DEFAULT_CAMERA_HAL_STATIC_PROPERTIES_H_
