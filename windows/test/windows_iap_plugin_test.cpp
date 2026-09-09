#include "windows_iap_plugin.h"

#include <flutter/standard_method_codec.h>

#include <windows.h>

#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Services.Store.h>

namespace {

using flutter::EncodableMap;
using flutter::EncodableValue;
using windows_iap::StoreApi;
using windows_iap::WindowsIapPlugin;
using namespace std::chrono_literals;
using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Services::Store;

enum class ResponseKind {
  kSuccess,
  kError,
  kNotImplemented,
  kDecodeFailure,
};

struct Response {
  ResponseKind kind = ResponseKind::kDecodeFailure;
  EncodableValue value;
  std::string error_code;
};

class ResponseResult final : public flutter::MethodResult<EncodableValue> {
 public:
  explicit ResponseResult(std::shared_ptr<std::promise<Response>> completion)
      : completion_(std::move(completion)) {}

 protected:
  void SuccessInternal(const EncodableValue* result) override {
    Response response;
    response.kind = ResponseKind::kSuccess;
    if (result != nullptr) {
      response.value = *result;
    }
    completion_->set_value(std::move(response));
  }

  void ErrorInternal(const std::string& error_code,
                     const std::string&,
                     const EncodableValue*) override {
    Response response;
    response.kind = ResponseKind::kError;
    response.error_code = error_code;
    completion_->set_value(std::move(response));
  }

  void NotImplementedInternal() override {
    Response response;
    response.kind = ResponseKind::kNotImplemented;
    completion_->set_value(std::move(response));
  }

 private:
  std::shared_ptr<std::promise<Response>> completion_;
};

class TestBinaryMessenger final : public flutter::BinaryMessenger {
 public:
  void Send(const std::string&,
            const uint8_t*,
            size_t,
            flutter::BinaryReply) const override {}

  void SetMessageHandler(const std::string& channel,
                         flutter::BinaryMessageHandler handler) override {
    if (channel == "windows_store_iap") {
      handler_ = std::move(handler);
    }
  }

  std::future<Response> Invoke(const std::string& method,
                               EncodableMap arguments = {}) {
    auto completion = std::make_shared<std::promise<Response>>();
    auto future = completion->get_future();
    if (!handler_) {
      Response response;
      response.kind = ResponseKind::kNotImplemented;
      completion->set_value(std::move(response));
      return future;
    }

    flutter::MethodCall<EncodableValue> call(
        method, std::make_unique<EncodableValue>(std::move(arguments)));
    const auto& codec = flutter::StandardMethodCodec::GetInstance();
    const auto message = codec.EncodeMethodCall(call);
    handler_(
        message->data(), message->size(),
        [completion, &codec](const uint8_t* reply, size_t reply_size) {
          ResponseResult result(completion);
          if (!codec.DecodeAndProcessResponseEnvelope(
                  reply, reply_size, &result)) {
            Response response;
            response.kind = ResponseKind::kDecodeFailure;
            completion->set_value(std::move(response));
          }
        });
    return future;
  }

 private:
  flutter::BinaryMessageHandler handler_;
};

struct FakeStoreLicense
    : implements<FakeStoreLicense, IStoreLicense> {
  FakeStoreLicense(hstring sku_store_id, DateTime expiration_date)
      : sku_store_id_(std::move(sku_store_id)),
        expiration_date_(expiration_date) {}

  hstring SkuStoreId() const { return sku_store_id_; }
  bool IsActive() const { return true; }
  DateTime ExpirationDate() const { return expiration_date_; }
  hstring ExtendedJsonData() const { return L"test-data"; }
  hstring InAppOfferToken() const { return L"test-offer"; }

 private:
  hstring sku_store_id_;
  DateTime expiration_date_;
};

struct FakeAppLicense
    : implements<FakeAppLicense, IStoreAppLicense> {
  explicit FakeAppLicense(
      Windows::Foundation::Collections::IMapView<hstring, StoreLicense>
          add_on_licenses)
      : add_on_licenses_(std::move(add_on_licenses)) {}

  hstring SkuStoreId() const { return L"app-sku"; }
  bool IsActive() const { return true; }
  bool IsTrial() const { return false; }
  DateTime ExpirationDate() const { return clock::now() + 24h; }
  hstring ExtendedJsonData() const { return L"test-data"; }
  Windows::Foundation::Collections::IMapView<hstring, StoreLicense>
  AddOnLicenses() const {
    return add_on_licenses_;
  }
  TimeSpan TrialTimeRemaining() const { return 0s; }
  bool IsTrialOwnedByThisUser() const { return false; }
  hstring TrialUniqueId() const { return L""; }

 private:
  Windows::Foundation::Collections::IMapView<hstring, StoreLicense>
      add_on_licenses_;
};

struct FakePurchaseResult
    : implements<FakePurchaseResult, IStorePurchaseResult> {
  FakePurchaseResult(StorePurchaseStatus status, hresult extended_error)
      : status_(status), extended_error_(extended_error) {}

  StorePurchaseStatus Status() const { return status_; }
  hresult ExtendedError() const { return extended_error_; }

 private:
  StorePurchaseStatus status_;
  hresult extended_error_;
};

class FakeStoreApi final : public StoreApi {
 public:
  FakeStoreApi(StoreAppLicense app_license,
               StorePurchaseStatus purchase_status =
                   StorePurchaseStatus::Succeeded,
               hresult purchase_error = S_OK,
               std::chrono::milliseconds delay = 20ms)
      : app_license_(std::move(app_license)),
        purchase_status_(purchase_status),
        purchase_error_(purchase_error),
        delay_(delay) {}

  IAsyncOperation<StorePurchaseResult> RequestPurchaseAsync(
      hstring store_id) override {
    last_purchase_id_ = std::move(store_id);
    co_await resume_after(delay_);
    auto purchase_interface =
        make<FakePurchaseResult>(purchase_status_, purchase_error_);
    StorePurchaseResult purchase{nullptr};
    attach_abi(purchase, detach_abi(purchase_interface));
    co_return purchase;
  }

  IAsyncOperation<StoreProductQueryResult> GetProductsAsync() override {
    co_return StoreProductQueryResult{nullptr};
  }

  IAsyncOperation<StoreAppLicense> GetAppLicenseAsync() override {
    co_await resume_after(delay_);
    co_return app_license_;
  }

  hstring last_purchase_id() const { return last_purchase_id_; }

 private:
  StoreAppLicense app_license_;
  StorePurchaseStatus purchase_status_;
  hresult purchase_error_;
  std::chrono::milliseconds delay_;
  hstring last_purchase_id_;
};

Response WaitFor(std::future<Response> future) {
  if (future.wait_for(3s) != std::future_status::ready) {
    Response response;
    response.kind = ResponseKind::kDecodeFailure;
    response.error_code = "timeout";
    return response;
  }
  return future.get();
}

EncodableMap StoreIdArguments(const std::string& store_id) {
  return {{EncodableValue("storeId"), EncodableValue(store_id)}};
}

StoreAppLicense CreateAppLicense(
    const std::string& product_store_id = "test-product") {
  auto licenses = single_threaded_map<hstring, StoreLicense>();
  auto license_interface = make<FakeStoreLicense>(
      to_hstring(product_store_id + "/sku"), clock::now() + 1h);
  StoreLicense license{nullptr};
  attach_abi(license, detach_abi(license_interface));
  licenses.Insert(L"collection-key", license);

  auto app_license_interface = make<FakeAppLicense>(licenses.GetView());
  StoreAppLicense app_license{nullptr};
  attach_abi(app_license, detach_abi(app_license_interface));
  return app_license;
}

struct TestWindow {
  HWND root = nullptr;
  HWND child = nullptr;

  TestWindow() {
    root = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED,
                           0, 0, 100, 100, nullptr, nullptr,
                           GetModuleHandleW(nullptr), nullptr);
    if (root != nullptr) {
      child = CreateWindowExW(0, L"STATIC", L"", WS_CHILD,
                              0, 0, 50, 50, root, nullptr,
                              GetModuleHandleW(nullptr), nullptr);
    }
  }

  ~TestWindow() {
    if (child != nullptr) {
      DestroyWindow(child);
    }
    if (root != nullptr) {
      DestroyWindow(root);
    }
  }

  TestWindow(const TestWindow&) = delete;
  TestWindow& operator=(const TestWindow&) = delete;
};

#define TEST_CHECK(condition)                                                \
  do {                                                                       \
    if (!(condition)) {                                                      \
      std::cerr << __FUNCTION__ << ":" << __LINE__                          \
                << ": check failed: " #condition << std::endl;              \
      return false;                                                          \
    }                                                                        \
  } while (false)

bool NoViewReturnsAnErrorWithoutCreatingStore() {
  TestBinaryMessenger messenger;
  bool factory_called = false;
  WindowsIapPlugin plugin(
      &messenger, [] { return static_cast<HWND>(nullptr); },
      [&factory_called](HWND) {
        factory_called = true;
        return std::shared_ptr<StoreApi>();
      });

  const Response response = WaitFor(messenger.Invoke("getProducts"));
  TEST_CHECK(response.kind == ResponseKind::kError);
  TEST_CHECK(response.error_code == "0x80070578");
  TEST_CHECK(!factory_called);
  return true;
}

bool PluginInstancesUseTheirOwnWindows() {
  TestWindow first_window;
  TestWindow second_window;
  TEST_CHECK(first_window.root != nullptr && first_window.child != nullptr);
  TEST_CHECK(second_window.root != nullptr && second_window.child != nullptr);

  auto first_store = std::make_shared<FakeStoreApi>(CreateAppLicense());
  auto second_store = std::make_shared<FakeStoreApi>(CreateAppLicense());
  HWND first_factory_window = nullptr;
  HWND second_factory_window = nullptr;
  TestBinaryMessenger first_messenger;
  TestBinaryMessenger second_messenger;

  {
    WindowsIapPlugin first_plugin(
        &first_messenger, [&first_window] { return first_window.child; },
        [&](HWND window) {
          first_factory_window = window;
          return first_store;
        });
    WindowsIapPlugin second_plugin(
        &second_messenger, [&second_window] { return second_window.child; },
        [&](HWND window) {
          second_factory_window = window;
          return second_store;
        });

    const Response first =
        WaitFor(first_messenger.Invoke("getAppLicense"));
    const Response second =
        WaitFor(second_messenger.Invoke("getAppLicense"));
    TEST_CHECK(first.kind == ResponseKind::kSuccess);
    TEST_CHECK(second.kind == ResponseKind::kSuccess);
  }

  TEST_CHECK(first_factory_window == first_window.root);
  TEST_CHECK(second_factory_window == second_window.root);
  TEST_CHECK(first_factory_window != second_factory_window);
  return true;
}

bool CheckPurchaseOwnsStoreIdAcrossSuspension() {
  const std::string product_store_id = "9TESTPRODUCT";
  auto store = std::make_shared<FakeStoreApi>(
      CreateAppLicense(product_store_id),
      StorePurchaseStatus::Succeeded, S_OK, 75ms);
  TestWindow window;
  TEST_CHECK(window.child != nullptr);
  TestBinaryMessenger messenger;
  WindowsIapPlugin plugin(
      &messenger, [&window] { return window.child; },
      [store](HWND) { return store; });

  const Response response = WaitFor(
      messenger.Invoke("checkPurchase",
                       StoreIdArguments(product_store_id)));
  TEST_CHECK(response.kind == ResponseKind::kSuccess);
  const auto* entitled = std::get_if<bool>(&response.value);
  TEST_CHECK(entitled != nullptr && *entitled);
  return true;
}

bool PurchaseStatusAndExtendedErrorAreBothAvailable() {
  auto store = std::make_shared<FakeStoreApi>(
      CreateAppLicense(), StorePurchaseStatus::NetworkError,
      hresult{E_FAIL}, 30ms);
  TestWindow window;
  TEST_CHECK(window.child != nullptr);
  TestBinaryMessenger messenger;
  WindowsIapPlugin plugin(
      &messenger, [&window] { return window.child; },
      [store](HWND) { return store; });

  const Response legacy = WaitFor(
      messenger.Invoke("makePurchase", StoreIdArguments("test-product")));
  TEST_CHECK(legacy.kind == ResponseKind::kSuccess);
  const auto* legacy_status = std::get_if<int32_t>(&legacy.value);
  TEST_CHECK(legacy_status != nullptr);
  TEST_CHECK(*legacy_status ==
             static_cast<int32_t>(StorePurchaseStatus::NetworkError));

  const Response detailed = WaitFor(
      messenger.Invoke("makePurchaseDetailed",
                       StoreIdArguments("test-product")));
  TEST_CHECK(detailed.kind == ResponseKind::kSuccess);
  const auto* details = std::get_if<EncodableMap>(&detailed.value);
  TEST_CHECK(details != nullptr);
  const auto status_entry = details->find(EncodableValue("status"));
  const auto error_entry =
      details->find(EncodableValue("extendedErrorCode"));
  TEST_CHECK(status_entry != details->end());
  TEST_CHECK(error_entry != details->end());
  const auto* status = std::get_if<int32_t>(&status_entry->second);
  const auto* extended_error =
      std::get_if<std::string>(&error_entry->second);
  TEST_CHECK(status != nullptr);
  TEST_CHECK(*status ==
             static_cast<int32_t>(StorePurchaseStatus::NetworkError));
  TEST_CHECK(extended_error != nullptr);
  TEST_CHECK(*extended_error == "0x80004005");
  TEST_CHECK(store->last_purchase_id() == L"test-product");
  return true;
}

bool ConcurrentPurchasesAreRejected() {
  auto store = std::make_shared<FakeStoreApi>(
      CreateAppLicense(), StorePurchaseStatus::Succeeded, S_OK, 150ms);
  TestWindow window;
  TEST_CHECK(window.child != nullptr);
  TestBinaryMessenger messenger;
  WindowsIapPlugin plugin(
      &messenger, [&window] { return window.child; },
      [store](HWND) { return store; });

  auto first =
      messenger.Invoke("makePurchase", StoreIdArguments("test-product"));
  const Response second = WaitFor(
      messenger.Invoke("makePurchase", StoreIdArguments("test-product")));
  TEST_CHECK(second.kind == ResponseKind::kError);
  TEST_CHECK(second.error_code == "purchase_in_progress");

  const Response first_response = WaitFor(std::move(first));
  TEST_CHECK(first_response.kind == ResponseKind::kSuccess);
  return true;
}

}  // namespace

int main() {
  winrt::init_apartment(winrt::apartment_type::multi_threaded);

  const std::pair<const char*, bool (*)()> tests[] = {
      {"NoViewReturnsAnErrorWithoutCreatingStore",
       NoViewReturnsAnErrorWithoutCreatingStore},
      {"PluginInstancesUseTheirOwnWindows",
       PluginInstancesUseTheirOwnWindows},
      {"CheckPurchaseOwnsStoreIdAcrossSuspension",
       CheckPurchaseOwnsStoreIdAcrossSuspension},
      {"PurchaseStatusAndExtendedErrorAreBothAvailable",
       PurchaseStatusAndExtendedErrorAreBothAvailable},
      {"ConcurrentPurchasesAreRejected",
       ConcurrentPurchasesAreRejected},
  };

  for (const auto& test : tests) {
    if (!test.second()) {
      return 1;
    }
    std::cout << "[PASS] " << test.first << std::endl;
  }
  return 0;
}
