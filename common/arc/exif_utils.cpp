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

#include "exif_utils.h"

#define LOG_TAG "android.hardware.camera.common@1.0-arc.stm32mpu"
// #define LOG_NDEBUG 0
#include <utils/Log.h>

#include <cstdlib>
#include <ctime>

#include <libyuv.h>

namespace std {

template <>
struct default_delete<ExifEntry> {
  inline void operator()(ExifEntry* entry) const { exif_entry_unref(entry); }
};

}  // namespace std

namespace android {
namespace hardware {
namespace camera {
namespace common {
namespace V1_0 {
namespace arc {

// This comes from the Exif Version 2.3 standard table 9.
const uint8_t gExifAsciiPrefix[] = {0x41, 0x53, 0x43, 0x49,
                                    0x49, 0x0,  0x0,  0x0};

static void SetLatitudeOrLongitudeData(unsigned char* data, double num) {
  // Take the integer part of |num|.
  ExifLong degrees = static_cast<ExifLong>(num);
  ExifLong minutes = static_cast<ExifLong>(60 * (num - degrees));
  ExifLong microseconds =
      static_cast<ExifLong>(3600000000u * (num - degrees - minutes / 60.0));
  exif_set_rational(data, EXIF_BYTE_ORDER_INTEL, {degrees, 1});
  exif_set_rational(data + sizeof(ExifRational), EXIF_BYTE_ORDER_INTEL,
                    {minutes, 1});
  exif_set_rational(data + 2 * sizeof(ExifRational), EXIF_BYTE_ORDER_INTEL,
                    {microseconds, 1000000});
}

ExifUtils::ExifUtils()
    : yu12_buffer_(nullptr),
      yu12_width_(0),
      yu12_height_(0),
      thumbnail_width_(0),
      thumbnail_height_(0),
      exif_data_(nullptr),
      app1_buffer_(nullptr),
      app1_length_(0) {}

ExifUtils::~ExifUtils() { Reset(); }

bool ExifUtils::Initialize(const uint8_t* buffer, uint16_t width,
                           uint16_t height, int quality) {
  Reset();

  if (width % 2 != 0 || height % 2 != 0) {
    ALOGE("%s: invalid image size %dx%d", __FUNCTION__, width, height);
    return false;
  }
  if (quality < 1 || quality > 100) {
    ALOGE("%s: invalid jpeg quality %d", __FUNCTION__, quality);
    return false;
  }
  thumbnail_jpeg_quality_ = quality;
  yu12_buffer_ = buffer;
  yu12_width_ = width;
  yu12_height_ = height;

  exif_data_ = exif_data_new();
  if (exif_data_ == nullptr) {
    ALOGE("%s: allocate memory for exif_data_ failed", __FUNCTION__);
    return false;
  }
  // Set the image options.
  exif_data_set_option(exif_data_, EXIF_DATA_OPTION_FOLLOW_SPECIFICATION);
  exif_data_set_data_type(exif_data_, EXIF_DATA_TYPE_COMPRESSED);
  exif_data_set_byte_order(exif_data_, EXIF_BYTE_ORDER_INTEL);

  // Set image width and length.
  SetImageWidth(width);
  SetImageLength(height);

  return true;
}

bool ExifUtils::SetMaker(const std::string& maker) {
  size_t entrySize = maker.length() + 1;
  std::unique_ptr<ExifEntry> entry = AddVariableLengthEntry(
      EXIF_IFD_0, EXIF_TAG_MAKE, EXIF_FORMAT_ASCII, entrySize, entrySize);
  if (!entry) {
    ALOGE("%s: Adding Make exif entry failed", __FUNCTION__);
    return false;
  }
  memcpy(entry->data, maker.c_str(), entrySize);
  return true;
}

bool ExifUtils::SetModel(const std::string& model) {
  size_t entrySize = model.length() + 1;
  std::unique_ptr<ExifEntry> entry = AddVariableLengthEntry(
      EXIF_IFD_0, EXIF_TAG_MODEL, EXIF_FORMAT_ASCII, entrySize, entrySize);
  if (!entry) {
    ALOGE("%s: Adding Model exif entry failed", __FUNCTION__);
    return false;
  }
  memcpy(entry->data, model.c_str(), entrySize);
  return true;
}

bool ExifUtils::SetDateTime(const struct tm& t) {
  // The length is 20 bytes including NULL for termination in Exif standard.
  char str[20];
  int result = snprintf(str, sizeof(str), "%04i:%02i:%02i %02i:%02i:%02i",
                        t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour,
                        t.tm_min, t.tm_sec);
  if (result != sizeof(str) - 1) {
    ALOGW("%s: Input time is invalid", __FUNCTION__);
    return false;
  }
  std::unique_ptr<ExifEntry> entry =
      AddVariableLengthEntry(EXIF_IFD_0, EXIF_TAG_DATE_TIME, EXIF_FORMAT_ASCII,
                             sizeof(str), sizeof(str));
  if (!entry) {
    ALOGE("%s: Adding DateTime exif entry failed", __FUNCTION__);
    return false;
  }
  memcpy(entry->data, str, sizeof(str));
  return true;
}

bool ExifUtils::SetFocalLength(uint32_t numerator, uint32_t denominator) {
  std::unique_ptr<ExifEntry> entry =
      AddEntry(EXIF_IFD_EXIF, EXIF_TAG_FOCAL_LENGTH);
  if (!entry) {
    ALOGE("%s: Adding FocalLength exif entry failed", __FUNCTION__);
    return false;
  }
  exif_set_rational(entry->data, EXIF_BYTE_ORDER_INTEL,
                    {numerator, denominator});
  return true;
}

bool ExifUtils::SetGpsLatitude(double latitude) {
  const ExifTag refTag = static_cast<ExifTag>(EXIF_TAG_GPS_LATITUDE_REF);
  std::unique_ptr<ExifEntry> refEntry =
      AddVariableLengthEntry(EXIF_IFD_GPS, refTag, EXIF_FORMAT_ASCII, 2, 2);
  if (!refEntry) {
    ALOGE("%s: Adding GPSLatitudeRef exif entry failed", __FUNCTION__);
    return false;
  }
  if (latitude >= 0) {
    memcpy(refEntry->data, "N", sizeof("N"));
  } else {
    memcpy(refEntry->data, "S", sizeof("S"));
    latitude *= -1;
  }

  const ExifTag tag = static_cast<ExifTag>(EXIF_TAG_GPS_LATITUDE);
  std::unique_ptr<ExifEntry> entry = AddVariableLengthEntry(
      EXIF_IFD_GPS, tag, EXIF_FORMAT_RATIONAL, 3, 3 * sizeof(ExifRational));
  if (!entry) {
    exif_content_remove_entry(exif_data_->ifd[EXIF_IFD_GPS], refEntry.get());
    ALOGE("%s: Adding GPSLatitude exif entry failed", __FUNCTION__);
    return false;
  }
  SetLatitudeOrLongitudeData(entry->data, latitude);

  return true;
}

bool ExifUtils::SetGpsLongitude(double longitude) {
  ExifTag refTag = static_cast<ExifTag>(EXIF_TAG_GPS_LONGITUDE_REF);
  std::unique_ptr<ExifEntry> refEntry =
      AddVariableLengthEntry(EXIF_IFD_GPS, refTag, EXIF_FORMAT_ASCII, 2, 2);
  if (!refEntry) {
    ALOGE("%s: Adding GPSLongitudeRef exif entry failed", __FUNCTION__);
    return false;
  }
  if (longitude >= 0) {
    memcpy(refEntry->data, "E", sizeof("E"));
  } else {
    memcpy(refEntry->data, "W", sizeof("W"));
    longitude *= -1;
  }

  ExifTag tag = static_cast<ExifTag>(EXIF_TAG_GPS_LONGITUDE);
  std::unique_ptr<ExifEntry> entry = AddVariableLengthEntry(
      EXIF_IFD_GPS, tag, EXIF_FORMAT_RATIONAL, 3, 3 * sizeof(ExifRational));
  if (!entry) {
    exif_content_remove_entry(exif_data_->ifd[EXIF_IFD_GPS], refEntry.get());
    ALOGE("%s: Adding GPSLongitude exif entry failed", __FUNCTION__);
    return false;
  }
  SetLatitudeOrLongitudeData(entry->data, longitude);

  return true;
}

bool ExifUtils::SetGpsAltitude(double altitude) {
  ExifTag refTag = static_cast<ExifTag>(EXIF_TAG_GPS_ALTITUDE_REF);
  std::unique_ptr<ExifEntry> refEntry =
      AddVariableLengthEntry(EXIF_IFD_GPS, refTag, EXIF_FORMAT_BYTE, 1, 1);
  if (!refEntry) {
    ALOGE("%s: adding GPSAltitudeRef exif entry failed", __FUNCTION__);
    return false;
  }
  if (altitude >= 0) {
    *refEntry->data = 0;
  } else {
    *refEntry->data = 1;
    altitude *= -1;
  }

  ExifTag tag = static_cast<ExifTag>(EXIF_TAG_GPS_ALTITUDE);
  std::unique_ptr<ExifEntry> entry = AddVariableLengthEntry(
      EXIF_IFD_GPS, tag, EXIF_FORMAT_RATIONAL, 1, sizeof(ExifRational));
  if (!entry) {
    exif_content_remove_entry(exif_data_->ifd[EXIF_IFD_GPS], refEntry.get());
    ALOGE("%s: Adding GPSAltitude exif entry failed", __FUNCTION__);
    return false;
  }
  exif_set_rational(entry->data, EXIF_BYTE_ORDER_INTEL,
                    {static_cast<ExifLong>(altitude * 1000), 1000});

  return true;
}

bool ExifUtils::SetGpsTimestamp(const struct tm& t) {
  const ExifTag dateTag = static_cast<ExifTag>(EXIF_TAG_GPS_DATE_STAMP);
  const size_t kGpsDateStampSize = 11;
  std::unique_ptr<ExifEntry> entry =
      AddVariableLengthEntry(EXIF_IFD_GPS, dateTag, EXIF_FORMAT_ASCII,
                             kGpsDateStampSize, kGpsDateStampSize);
  if (!entry) {
    ALOGE("%s: Adding GPSDateStamp exif entry failed", __FUNCTION__);
    return false;
  }
  int result =
      snprintf(reinterpret_cast<char*>(entry->data), kGpsDateStampSize,
               "%04i:%02i:%02i", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  if (result != kGpsDateStampSize - 1) {
    ALOGW("%s: Input time is invalid", __FUNCTION__);
    return false;
  }

  const ExifTag timeTag = static_cast<ExifTag>(EXIF_TAG_GPS_TIME_STAMP);
  entry = AddVariableLengthEntry(EXIF_IFD_GPS, timeTag, EXIF_FORMAT_RATIONAL, 3,
                                 3 * sizeof(ExifRational));
  if (!entry) {
    ALOGE("%s: Adding GPSTimeStamp exif entry failed", __FUNCTION__);
    return false;
  }
  exif_set_rational(entry->data, EXIF_BYTE_ORDER_INTEL,
                    {static_cast<ExifLong>(t.tm_hour), 1});
  exif_set_rational(entry->data + sizeof(ExifRational), EXIF_BYTE_ORDER_INTEL,
                    {static_cast<ExifLong>(t.tm_min), 1});
  exif_set_rational(entry->data + 2 * sizeof(ExifRational),
                    EXIF_BYTE_ORDER_INTEL,
                    {static_cast<ExifLong>(t.tm_sec), 1});

  return true;
}

bool ExifUtils::SetGpsProcessingMethod(const std::string& method) {
  ExifTag tag = static_cast<ExifTag>(EXIF_TAG_GPS_PROCESSING_METHOD);
  size_t size = sizeof(gExifAsciiPrefix) + method.length();
  std::unique_ptr<ExifEntry> entry = AddVariableLengthEntry(
      EXIF_IFD_GPS, tag, EXIF_FORMAT_UNDEFINED, size, size);
  if (!entry) {
    ALOGE("%s: Adding GPSProcessingMethod exif entry failed", __FUNCTION__);
    return false;
  }
  memcpy(entry->data, gExifAsciiPrefix, sizeof(gExifAsciiPrefix));
  // Since the exif format is undefined, NULL termination is not necessary.
  memcpy(entry->data + sizeof(gExifAsciiPrefix), method.c_str(),
         method.length());

  return true;
}

bool ExifUtils::SetThumbnailSize(uint16_t width, uint16_t height) {
  if (width % 2 != 0 || height % 2 != 0) {
    ALOGE("%s: Invalid thumbnail size %dx%d", __FUNCTION__, width, height);
    return false;
  }
  thumbnail_width_ = width;
  thumbnail_height_ = height;
  return true;
}

bool ExifUtils::SetOrientation(uint16_t orientation) {
  std::unique_ptr<ExifEntry> entry = AddEntry(EXIF_IFD_0, EXIF_TAG_ORIENTATION);
  if (!entry) {
    ALOGE("%s: Adding Orientation exif entry failed", __FUNCTION__);
    return false;
  }
  /*
   * Orientation value:
   *  1      2      3      4      5          6          7          8
   *
   *  888888 888888     88 88     8888888888 88                 88 8888888888
   *  88         88     88 88     88  88     88  88         88  88     88  88
   *  8888     8888   8888 8888   88         8888888888 8888888888         88
   *  88         88     88 88
   *  88         88 888888 888888
   */
  int value = 1;
  switch (orientation) {
    case 90:
      value = 6;
      break;
    case 180:
      value = 3;
      break;
    case 270:
      value = 8;
      break;
    default:
      break;
  }
  exif_set_short(entry->data, EXIF_BYTE_ORDER_INTEL, value);
  return true;
}

bool ExifUtils::GenerateApp1() {
  DestroyApp1();
  if (thumbnail_width_ > 0 && thumbnail_height_ > 0) {
    if (!GenerateThumbnail()) {
      ALOGE("%s: Generate thumbnail image failed", __FUNCTION__);
      return false;
    }
    exif_data_->data = const_cast<uint8_t*>(
        static_cast<const uint8_t*>(compressor_.GetCompressedImagePtr()));
    exif_data_->size = compressor_.GetCompressedImageSize();
  }
  // Save the result into |app1_buffer_|.
  exif_data_save_data(exif_data_, &app1_buffer_, &app1_length_);
  if (!app1_length_) {
    ALOGE("%s: Allocate memory for app1_buffer_ failed", __FUNCTION__);
    return false;
  }
  /*
   * The JPEG segment size is 16 bits in spec. The size of APP1 segment should
   * be smaller than 65533 because there are two bytes for segment size field.
   */
  if (app1_length_ > 65533) {
    DestroyApp1();
    ALOGE("%s: The size of APP1 segment is too large", __FUNCTION__);
    return false;
  }
  return true;
}

const uint8_t* ExifUtils::GetApp1Buffer() { return app1_buffer_; }

unsigned int ExifUtils::GetApp1Length() { return app1_length_; }

void ExifUtils::Reset() {
  yu12_buffer_ = nullptr;
  yu12_width_ = 0;
  yu12_height_ = 0;
  thumbnail_width_ = 0;
  thumbnail_height_ = 0;
  DestroyApp1();
  if (exif_data_) {
    /*
     * Since we decided to ignore the original APP1, we are sure that there is
     * no thumbnail allocated by libexif. |exif_data_->data| is actually
     * allocated by JpegCompressor. Sets |exif_data_->data| to nullptr to
     * prevent exif_data_unref() destroy it incorrectly.
     */
    exif_data_->data = nullptr;
    exif_data_->size = 0;
    exif_data_unref(exif_data_);
    exif_data_ = nullptr;
  }
}

std::unique_ptr<ExifEntry> ExifUtils::AddVariableLengthEntry(
    ExifIfd ifd, ExifTag tag, ExifFormat format, uint64_t components,
    unsigned int size) {
  // Remove old entry if exists.
  exif_content_remove_entry(exif_data_->ifd[ifd],
                            exif_content_get_entry(exif_data_->ifd[ifd], tag));
  ExifMem* mem = exif_mem_new_default();
  if (!mem) {
    ALOGE("%s: Allocate memory for exif entry failed", __FUNCTION__);
    return nullptr;
  }
  std::unique_ptr<ExifEntry> entry(exif_entry_new_mem(mem));
  if (!entry) {
    ALOGE("%s: Allocate memory for exif entry failed", __FUNCTION__);
    exif_mem_unref(mem);
    return nullptr;
  }
  void* tmpBuffer = exif_mem_alloc(mem, size);
  if (!tmpBuffer) {
    ALOGE("%s: Allocate memory for exif entry failed", __FUNCTION__);
    exif_mem_unref(mem);
    return nullptr;
  }

  entry->data = static_cast<unsigned char*>(tmpBuffer);
  entry->tag = tag;
  entry->format = format;
  entry->components = components;
  entry->size = size;

  exif_content_add_entry(exif_data_->ifd[ifd], entry.get());
  exif_mem_unref(mem);

  return entry;
}

std::unique_ptr<ExifEntry> ExifUtils::AddEntry(ExifIfd ifd, ExifTag tag) {
  std::unique_ptr<ExifEntry> entry(
      exif_content_get_entry(exif_data_->ifd[ifd], tag));
  if (entry) {
    // exif_content_get_entry() won't ref the entry, so we ref here.
    exif_entry_ref(entry.get());
    return entry;
  }
  entry.reset(exif_entry_new());
  if (!entry) {
    ALOGE("%s: Allocate memory for exif entry failed", __FUNCTION__);
    return nullptr;
  }
  entry->tag = tag;
  exif_content_add_entry(exif_data_->ifd[ifd], entry.get());
  exif_entry_initialize(entry.get(), tag);
  return entry;
}

bool ExifUtils::SetImageWidth(uint16_t width) {
  std::unique_ptr<ExifEntry> entry = AddEntry(EXIF_IFD_0, EXIF_TAG_IMAGE_WIDTH);
  if (!entry) {
    ALOGE("%s: Adding ImageWidth exif entry failed", __FUNCTION__);
    return false;
  }
  exif_set_short(entry->data, EXIF_BYTE_ORDER_INTEL, width);
  return true;
}

bool ExifUtils::SetImageLength(uint16_t length) {
  std::unique_ptr<ExifEntry> entry =
      AddEntry(EXIF_IFD_0, EXIF_TAG_IMAGE_LENGTH);
  if (!entry) {
    ALOGE("%s: Adding ImageLength exif entry failed", __FUNCTION__);
    return false;
  }
  exif_set_short(entry->data, EXIF_BYTE_ORDER_INTEL, length);
  return true;
}

bool ExifUtils::GenerateThumbnail() {
  // Resize yuv image to |thumbnail_width_| x |thumbnail_height_|.
  std::vector<uint8_t> scaled_buffer;
  if (!GenerateYuvThumbnail(&scaled_buffer)) {
    ALOGE("%s: Generate YUV thumbnail failed", __FUNCTION__);
    return false;
  }

  // Compress thumbnail to JPEG.
  if (!compressor_.CompressImage(scaled_buffer.data(), thumbnail_width_,
                                 thumbnail_height_, thumbnail_jpeg_quality_,
                                 NULL, 0)) {
    ALOGE("%s: Compress thumbnail failed", __FUNCTION__);
    return false;
  }
  return true;
}

bool ExifUtils::GenerateYuvThumbnail(std::vector<uint8_t>* scaled_buffer) {
  size_t y_plane_size = yu12_width_ * yu12_height_;
  const uint8_t* y_plane = yu12_buffer_;
  const uint8_t* u_plane = y_plane + y_plane_size;
  const uint8_t* v_plane = u_plane + y_plane_size / 4;

  size_t scaled_y_plane_size = thumbnail_width_ * thumbnail_height_;
  scaled_buffer->resize(scaled_y_plane_size * 3 / 2);
  uint8_t* scaled_y_plane = scaled_buffer->data();
  uint8_t* scaled_u_plane = scaled_y_plane + scaled_y_plane_size;
  uint8_t* scaled_v_plane = scaled_u_plane + scaled_y_plane_size / 4;

  int result = libyuv::I420Scale(
      y_plane, yu12_width_, u_plane, yu12_width_ / 2, v_plane, yu12_width_ / 2,
      yu12_width_, yu12_height_, scaled_y_plane, thumbnail_width_,
      scaled_u_plane, thumbnail_width_ / 2, scaled_v_plane,
      thumbnail_width_ / 2, thumbnail_width_, thumbnail_height_,
      libyuv::kFilterNone);
  if (result != 0) {
    ALOGE("%s: Scale I420 image failed", __FUNCTION__);
    return false;
  }
  return true;
}

void ExifUtils::DestroyApp1() {
  /*
   * Since there is no API to access ExifMem in ExifData->priv, we use free
   * here, which is the default free function in libexif. See
   * exif_data_save_data() for detail.
   */
  free(app1_buffer_);
  app1_buffer_ = nullptr;
  app1_length_ = 0;
}

} // namespace arc
} // namespace V1_0
} // namespace common
} // namespace camera
} // namespace hardware
} // namespace android
