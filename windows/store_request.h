#ifndef WINDOWS_STORE_IAP_STORE_REQUEST_H_
#define WINDOWS_STORE_IAP_STORE_REQUEST_H_

#include <flutter/encodable_value.h>
#include <flutter/method_result.h>
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.Threading.h>

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace windows_iap {

// Shares the reply across the coroutine, timeout and plugin shutdown. Only the
// first completion may reach Flutter; no callback owns the plugin itself.
class StoreRequest final
    : public flutter::MethodResult<flutter::EncodableValue>,
      public std::enable_shared_from_this<StoreRequest> {
 public:
  explicit StoreRequest(
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

  void Watch(winrt::Windows::Foundation::IAsyncInfo operation,
             std::chrono::milliseconds timeout);
  bool pending() const;
  void Shutdown();

 protected:
  void SuccessInternal(const flutter::EncodableValue* value) override;
  void ErrorInternal(const std::string& code, const std::string& message,
                     const flutter::EncodableValue* details) override;
  void NotImplementedInternal() override;

 private:
  using Result = flutter::MethodResult<flutter::EncodableValue>;
  void Finish(const std::function<void(Result&)>& reply, bool cancel);
  static void Cancel(
      const winrt::agile_ref<winrt::Windows::Foundation::IAsyncInfo>&
          operation) noexcept;

  mutable std::mutex mutex_;
  std::unique_ptr<Result> result_;
  winrt::agile_ref<winrt::Windows::Foundation::IAsyncInfo> operation_;
  winrt::Windows::System::Threading::ThreadPoolTimer timer_{nullptr};
};

class StoreRequestManager final {
 public:
  std::shared_ptr<StoreRequest> Add(
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  void Shutdown();

 private:
  std::vector<std::weak_ptr<StoreRequest>> requests_;
};

}  // namespace windows_iap

#endif  // WINDOWS_STORE_IAP_STORE_REQUEST_H_
