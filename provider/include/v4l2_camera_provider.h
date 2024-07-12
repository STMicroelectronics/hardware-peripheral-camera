#ifndef AIDL_ANDROID_HARDWARE_CAMERA_PROVIDER_V4L2_CAMERA_PROVIDER_H
#define AIDL_ANDROID_HARDWARE_CAMERA_PROVIDER_V4L2_CAMERA_PROVIDER_H

#include <aidl/android/hardware/camera/provider/BnCameraProvider.h>
#include <aidl/android/hardware/camera/provider/ICameraProviderCallback.h>
#include <aidl/android/hardware/camera/common/Status.h>

#include <unordered_map>
#include <regex>

#include <v4l2_camera_config.h>

namespace android {
namespace hardware {
namespace camera {
namespace provider {
namespace implementation {

using aidl::android::hardware::camera::common::Status;
using aidl::android::hardware::camera::common::VendorTagSection;
using aidl::android::hardware::camera::device::ICameraDevice;
using aidl::android::hardware::camera::provider::BnCameraProvider;
using aidl::android::hardware::camera::provider::CameraIdAndStreamCombination;
using aidl::android::hardware::camera::provider::ConcurrentCameraIdCombination;
using aidl::android::hardware::camera::provider::ICameraProviderCallback;

using ndk::ScopedAStatus;

using ::android::hardware::camera::device::implementation::V4l2CameraConfig;

class V4l2CameraProvider : public BnCameraProvider {
public:
  static const std::string kProviderName;
  static const std::regex kDeviceNameRegex;
  static std::shared_ptr<V4l2CameraProvider> Create();

  V4l2CameraProvider();
  virtual ~V4l2CameraProvider();

  // Override functions in ICameraProvider.

  ScopedAStatus setCallback(
      const std::shared_ptr<ICameraProviderCallback>& callback) override;

  ScopedAStatus getVendorTags(std::vector<VendorTagSection>* vts) override;

  ScopedAStatus getCameraIdList(std::vector<std::string>* camera_ids) override;

  ScopedAStatus getCameraDeviceInterface(
      const std::string& camera_device_name,
      std::shared_ptr<ICameraDevice>* device) override;

  ScopedAStatus notifyDeviceStateChange(int64_t device_state) override;

  ScopedAStatus getConcurrentCameraIds(
      std::vector<ConcurrentCameraIdCombination>* concurrent_camera_ids) override;

  ScopedAStatus isConcurrentStreamCombinationSupported(
      const std::vector<CameraIdAndStreamCombination>& configs,
      bool* support) override;

  // End of override functions in ICameraProvider.

private:
  Status initialize();

private:
  std::shared_ptr<ICameraProviderCallback> callback_;
  std::vector<VendorTagSection> vendor_tag_sections_;
  std::unordered_map<std::string, V4l2CameraConfig> v4l2_cameras_;

};

} // implementation
} // provider
} // camera
} // hardware
} // android

#endif // AIDL_ANDROID_HARDWARE_CAMERA_PROVIDER_V4L2_CAMERA_PROVIDER_H
