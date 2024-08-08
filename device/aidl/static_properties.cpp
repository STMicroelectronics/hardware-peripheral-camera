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

#define LOG_TAG "android.hardware.camera.device@1.1-metadata.stm32mpu"
// #define LOG_NDEBUG 0

#include "static_properties.h"

#include <aidl/android/hardware/camera/device/StreamType.h>
#include <aidl/android/hardware/camera/device/StreamConfigurationMode.h>

#include <utils/Log.h>

namespace android {
namespace hardware {
namespace camera {
namespace device {
namespace implementation {

using aidl::android::hardware::camera::device::StreamType;
using aidl::android::hardware::camera::device::StreamConfigurationMode;

using ::android::hardware::camera::common::V1_0::metadata::StreamStallDuration;
using StreamConfig =
      ::android::hardware::camera::common::V1_0::metadata::StreamConfiguration;

// Build stream capabilities from configs + stall durations.
static bool ConstructStreamCapabilities(
                              const std::vector<StreamConfig>& configs,
                              const std::vector<StreamStallDuration>& stalls,
                              StaticProperties::CapabilitiesMap* capabilities) {
  // Extract directional capabilities from the configs.
  for (const auto& config : configs) {
    switch (config.direction) {
      case ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT:
        (*capabilities)[config.spec].output_supported = true;
        break;
      case ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_INPUT:
        (*capabilities)[config.spec].input_supported = true;
        break;
      default:
        // Should never happen when using the MetadataReader;
        // it should validate directions.
        ALOGE("%s: Unrecognized stream config direction %d.",
                  __func__, config.direction);
        return false;
    }
  }

  // Extract stall durations from the stalls.
  for (const auto& stall : stalls) {
    (*capabilities)[stall.spec].stall_duration = stall.duration;
  }

  return true;
}

// Check that each output config has a valid corresponding stall duration
// (extra durations not matching any output config are ignored).
static bool ValidateStreamCapabilities(
    StaticProperties::CapabilitiesMap capabilities) {
  for (const auto& spec_capabilities : capabilities) {
    // Only non-negative stall durations are valid. This should only happen
    // due to output streams without an associated stall duration, as
    // MetadataReader validates the metadata stall durations.
    if (spec_capabilities.second.output_supported &&
        spec_capabilities.second.stall_duration < 0) {
      ALOGE(
          "%s: Static metadata does not have a stall duration for "
          "each output configuration. ",
          __func__);
      return false;
    }
  }
  return true;
}

// Validate that the input/output formats map matches up with
// the capabilities listed for all formats.
bool ValidateReprocessFormats(
    const StaticProperties::CapabilitiesMap& capabilities,
    const ReprocessFormatMap& reprocess_map) {
  // Get input formats.
  std::set<int32_t> all_input_formats;
  std::set<int32_t> all_output_formats;
  for (const auto& spec_capabilities : capabilities) {
    if (spec_capabilities.second.input_supported) {
      all_input_formats.insert(spec_capabilities.first.format);
    }
    if (spec_capabilities.second.output_supported) {
      all_output_formats.insert(spec_capabilities.first.format);
    }
  }

  // Must be at least one input format.
  if (all_input_formats.size() < 1) {
    ALOGE("%s: No input formats, reprocessing can't be supported.", __func__);
    return false;
  }

  // Check that the reprocess map input formats are exactly all available
  // input formats (check size here, then checking for actual value
  // matches will happen as part of the loop below).
  if (all_input_formats.size() != reprocess_map.size()) {
    ALOGE(
        "%s: Stream configuration input formats do not match "
        "input/output format map input formats.",
        __func__);
    return false;
  }

  // Check that each input format has at least one matching output format.
  for (const auto& input_format : all_input_formats) {
    const auto input_outputs_iterator = reprocess_map.find(input_format);
    if (input_outputs_iterator == reprocess_map.end()) {
      ALOGE(
          "%s: No output formats for input format %d.", __func__, input_format);
      return false;
    }
    // No need to check that the output formats vector is non-empty;
    // MetadataReader validates this. Instead just check that
    // all outputs are actually output formats.
    for (const auto& output_format : input_outputs_iterator->second) {
      if (all_output_formats.count(output_format) < 1) {
        ALOGE(
            "%s: Output format %d for input format %d "
            "is not a supported output format.",
            __func__,
            input_format,
            output_format);
        return false;
      }
    }
  }

  return true;
}

StaticProperties* StaticProperties::NewStaticProperties(
    std::unique_ptr<const MetadataReader> metadata_reader) {
  int facing = 0;
  int orientation = 0;
  int32_t max_input_streams = 0;
  int32_t max_raw_output_streams = 0;
  int32_t max_non_stalling_output_streams = 0;
  int32_t max_stalling_output_streams = 0;
  std::set<uint8_t> request_capabilities;
  std::vector<StreamConfig> configs;
  std::vector<StreamStallDuration> stalls;
  std::vector<int64_t> available_use_cases;
  std::vector<uint8_t> available_rotations;
  CapabilitiesMap stream_capabilities;
  ReprocessFormatMap reprocess_map;

  // If reading any data returns an error, something is wrong.
  if (metadata_reader->Facing(&facing) ||
      metadata_reader->Orientation(&orientation) ||
      metadata_reader->AvailableUseCases(&available_use_cases) ||
      metadata_reader->AvailableRotation(&available_rotations) ||
      metadata_reader->MaxInputStreams(&max_input_streams) ||
      metadata_reader->MaxOutputStreams(&max_raw_output_streams,
                                        &max_non_stalling_output_streams,
                                        &max_stalling_output_streams) ||
      metadata_reader->RequestCapabilities(&request_capabilities) ||
      metadata_reader->StreamConfigurations(&configs) ||
      metadata_reader->StreamStallDurations(&stalls) ||
      !ConstructStreamCapabilities(configs, stalls, &stream_capabilities) ||
      // MetadataReader validates configs and stall seperately,
      // but not that they match.
      !ValidateStreamCapabilities(stream_capabilities) ||
      // Reprocessing metadata only necessary if input streams are allowed.
      (max_input_streams > 0 &&
       (metadata_reader->ReprocessFormats(&reprocess_map) ||
        // MetadataReader validates configs and the reprocess map seperately,
        // but not that they match.
        !ValidateReprocessFormats(stream_capabilities, reprocess_map)))) {
    return nullptr;
  }

  return new StaticProperties(std::move(metadata_reader),
                              facing,
                              orientation,
                              available_use_cases,
                              available_rotations,
                              max_input_streams,
                              max_raw_output_streams,
                              max_non_stalling_output_streams,
                              max_stalling_output_streams,
                              std::move(request_capabilities),
                              std::move(stream_capabilities),
                              std::move(reprocess_map));
}

StaticProperties::StaticProperties(
    std::unique_ptr<const MetadataReader> metadata_reader,
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
    ReprocessFormatMap supported_reprocess_outputs)
    : metadata_reader_(std::move(metadata_reader)),
      facing_(facing),
      orientation_(orientation),
      available_use_cases_(available_use_cases),
      available_rotations_(available_rotations),
      max_input_streams_(max_input_streams),
      max_raw_output_streams_(max_raw_output_streams),
      max_non_stalling_output_streams_(max_non_stalling_output_streams),
      max_stalling_output_streams_(max_stalling_output_streams),
      request_capabilities_(std::move(request_capabilities)),
      stream_capabilities_(std::move(stream_capabilities)),
      supported_reprocess_outputs_(std::move(supported_reprocess_outputs)) {}

bool StaticProperties::TemplateSupported(RequestTemplate type) const {
  uint8_t required_capability = 0;

  switch (type) {
    case RequestTemplate::PREVIEW:
      // Preview is always supported.
      return true;
    case RequestTemplate::MANUAL:
      required_capability =
                          ANDROID_REQUEST_AVAILABLE_CAPABILITIES_MANUAL_SENSOR;
      break;
    case RequestTemplate::ZERO_SHUTTER_LAG:
      required_capability =
                    ANDROID_REQUEST_AVAILABLE_CAPABILITIES_PRIVATE_REPROCESSING;
      break;
    default:
      required_capability =
                    ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE;
      return true;
  }

  return request_capabilities_.count(required_capability) > 0;
}

// Helper functions for checking stream properties when verifying support.
static bool IsInputType(StreamType stream_type) {
  return stream_type == StreamType::INPUT;
}

static bool IsOutputType(StreamType stream_type) {
  return stream_type == StreamType::OUTPUT;
}

static bool IsRawFormat(PixelFormat format) {
  return format == PixelFormat::RAW10 || format == PixelFormat::RAW12 ||
         format == PixelFormat::RAW16 || format == PixelFormat::RAW_OPAQUE;
}

bool StaticProperties::StreamConfigurationSupported(
                                    const StreamConfiguration* stream_config) {
  return SanityCheckStreamConfiguration(stream_config) &&
         InputStreamsSupported(stream_config) &&
         OutputStreamsSupported(stream_config) &&
         OperationModeSupported(stream_config);
}

bool StaticProperties::SanityCheckStreamConfiguration(
                                    const StreamConfiguration* stream_config) {
  // Check for null/empty values.
  if (stream_config == nullptr) {
    ALOGE("%s: NULL stream configuration array", __func__);
    return false;
  } else if (stream_config->streams.size() == 0) {
    ALOGE("%s: Empty stream configuration array", __func__);
    return false;
  }

  // Check that all streams are either inputs or outputs (or both).
  for (size_t i = 0; i < stream_config->streams.size(); ++i) {
    const Stream& stream = stream_config->streams[i];

    if (!IsInputType(stream.streamType) &&
               !IsOutputType(stream.streamType)) {
      ALOGE("%s: Stream %zu type %d is neither an input nor an output type",
                                            __func__, i, stream.streamType);
      return false;
    }

    if (std::find(available_use_cases_.cbegin(), available_use_cases_.cend(),
                  static_cast<int64_t>(stream.useCase)) ==
        available_use_cases_.cend()) {
      ALOGE("%s: %ld use case is not supported", __func__, stream.useCase);
      return false;
    }
    if (std::find(available_rotations_.cbegin(), available_rotations_.cend(),
                  static_cast<int8_t>(stream.rotation)) ==
        available_rotations_.cend()) {
      ALOGE("%s: %d rotation is not supported", __func__, stream.rotation);
      return false;
    }
  }

  return true;
}

bool StaticProperties::InputStreamsSupported(
                                    const StreamConfiguration* stream_config) {
  // Find the input stream(s).
  int32_t num_input_streams = 0;
  int input_format = -1;

  for (size_t i = 0; i < stream_config->streams.size(); ++i) {
    const Stream& stream = stream_config->streams[i];

    if (IsInputType(stream.streamType)) {
      // Check that this stream is valid as an input.
      StreamSpec stream_spec(static_cast<int32_t>(stream.format),
                             stream.width, stream.height);
      const auto capabilities_iterator = stream_capabilities_.find(stream_spec);

      if (capabilities_iterator == stream_capabilities_.end() ||
                            !capabilities_iterator->second.input_supported) {
        ALOGE("%s: %d x %d stream of format %d is not a supported input setup.",
                      __func__, stream.width, stream.height, stream.format);
        return false;
      }

      // Valid input stream; count it.
      ++num_input_streams;
      input_format = static_cast<int>(stream.format);
    }
  }

  // Check the count.
  if (num_input_streams > max_input_streams_) {
    ALOGE(
        "%s: Requested number of input streams %d is greater than "
        "the maximum number supported by the device (%d).",
                            __func__, num_input_streams, max_input_streams_);
    return false;
  }

  if (num_input_streams > 1) {
    ALOGE("%s: Camera HAL 3.4 only supports 1 input stream max.", __func__);
    return false;
  }

  // If there's an input stream, the configuration must have at least one
  // supported output format for reprocessing that input.
  if (num_input_streams > 0) {
    const auto input_output_formats_iterator =
                                supported_reprocess_outputs_.find(input_format);

    if (input_output_formats_iterator == supported_reprocess_outputs_.end()) {
      // Should never happen; factory should verify that all valid inputs
      // have one or more valid outputs.
      ALOGE("%s: No valid output formats for input format %d.",
                                                        __func__, input_format);
      return false;
    }

    bool match_found = false;

    // Go through outputs looking for a supported one.
    for (size_t i = 0; i < stream_config->streams.size(); ++i) {
      const Stream& stream = stream_config->streams[i];

      if (IsOutputType(stream.streamType)) {
        if (input_output_formats_iterator->second.count(
                                        static_cast<int>(stream.format)) > 0) {
          match_found = true;
          break;
        }
      }
    }
    if (!match_found) {
      ALOGE("%s: No supported output format provided for input format %d.",
                                                        __func__, input_format);

      return false;
    }
  }

  return true;
}

bool StaticProperties::OutputStreamsSupported(
                                    const StreamConfiguration* stream_config) {
  // Find and count output streams.
  int32_t num_raw = 0;
  int32_t num_stalling = 0;
  int32_t num_non_stalling = 0;

  for (size_t i = 0; i < stream_config->streams.size(); ++i) {
    const Stream& stream = stream_config->streams[i];

    if (IsOutputType(stream.streamType)) {
      // Check that this stream is valid as an output.
      StreamSpec stream_spec(static_cast<int32_t>(stream.format),
                             stream.width, stream.height);
      const auto capabilities_iterator = stream_capabilities_.find(stream_spec);

      if (capabilities_iterator == stream_capabilities_.end() ||
                              !capabilities_iterator->second.output_supported) {
        ALOGE(
            "%s: %d x %d stream of format %d is not a supported output setup.",
                      __func__, stream.width, stream.height, stream.format);
        return false;
      }

      // Valid output; count it.
      if (IsRawFormat(stream.format)) {
        ++num_raw;
      } else if (capabilities_iterator->second.stall_duration > 0) {
        ++num_stalling;
      } else {
        ++num_non_stalling;
      }
    }
  }

  // Check that the counts are within bounds.
  if (num_raw > max_raw_output_streams_) {
    ALOGE(
        "%s: Requested stream configuration exceeds maximum supported "
        "raw output streams %d (requested %d).",
                                    __func__, max_raw_output_streams_, num_raw);
    return false;
  } else if (num_stalling > max_stalling_output_streams_) {
    ALOGE(
        "%s: Requested stream configuration exceeds maximum supported "
        "stalling output streams %d (requested %d).",
                          __func__, max_stalling_output_streams_, num_stalling);
    return false;
  } else if (num_non_stalling > max_non_stalling_output_streams_) {
    ALOGE(
        "%s: Requested stream configuration exceeds maximum supported "
        "non-stalling output streams %d (requested %d).",
                  __func__, max_non_stalling_output_streams_, num_non_stalling);
    return false;
  }

  return true;
}

bool StaticProperties::OperationModeSupported(
                                      const StreamConfiguration* stream_config) {
  switch (stream_config->operationMode) {
    case StreamConfigurationMode::NORMAL_MODE:
      return true;
    case StreamConfigurationMode::CONSTRAINED_HIGH_SPEED_MODE:
      // TODO(b/31370792): Check metadata for high speed support,
      // check that requested streams have support for high speed.
      ALOGE("%s: Support for CONSTRAINED_HIGH_SPEED not implemented", __func__);
      return false;
    default:
      ALOGE("%s: Unrecognized stream configuration mode: %d",
                                      __func__, stream_config->operationMode);
      return false;
  }
}

bool StaticProperties::ReprocessingSupported(
    const Stream* input_stream,
    const std::set<const Stream*>& output_streams) {
  // There must be an input.
  if (!input_stream) {
    ALOGE("%s: No input stream.", __func__);
    return false;
  }
  // There must be an output.
  if (output_streams.size() < 1) {
    ALOGE("%s: No output stream.", __func__);
    return false;
  }

  const auto input_output_formats =
      supported_reprocess_outputs_.find(static_cast<int>(input_stream->format));
  if (input_output_formats == supported_reprocess_outputs_.end()) {
    // Should never happen for a valid input stream.
    ALOGE("%s: Input format %d does not support any output formats.",
                                                __func__, input_stream->format);
    return false;
  }

  // Check that all output streams can be outputs for the input stream.
  const std::set<int32_t>& supported_output_formats =
                                                  input_output_formats->second;
  for (const auto output_stream : output_streams) {
    if (supported_output_formats.count(
                                static_cast<int>(output_stream->format)) < 1) {
      ALOGE(
          "%s: Output format %d is not a supported output "
          "for request input format %d.",
                        __func__, output_stream->format, input_stream->format);
      return false;
    }
  }

  return true;
}

} // namespace implementation
} // namespace device
} // namespace camera
} // namespace hardware
} // namespace android
