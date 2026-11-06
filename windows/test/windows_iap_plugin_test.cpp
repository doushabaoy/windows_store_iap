#include "windows_iap_plugin.h"

#include <flutter/standard_method_codec.h>
#include <windows.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Services.Store.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "store_catalog_fakes.h"

namespace {

using flutter::EncodableMap;
using flutter::EncodableValue;
using windows_iap::StoreApi;
using windows_iap::WindowsIapPlugin;
using windows_iap::testing::CatalogShape;
using windows_iap::testing::CatalogStoreApi;
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
  std::string error_message;
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

  void ErrorInternal(const std::string& error_code, const std::string& message,
                     const EncodableValue*) override {
    Response response;
    response.kind = ResponseKind::kError;
    response.error_code = error_code;
    response.error_message = message;
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
  void Send(const std::string&, const uint8_t*, size_t,
            flutter::BinaryReply) const override {}

  void SetMessageHandler(const std::string& channel,
                         flutter::BinaryMessageHandler handler) override {
    if (reject_handler_changes_) {
      handler_changed_while_rejected_ = true;
      return;
    }
    if (channel == "windows_store_iap") {
      handler_ = std::move(handler);
    }
  }

  int response_count() const { return replies_->load(); }

  void RejectFurtherHandlerChanges() { reject_handler_changes_ = true; }

  bool handler_changed_while_rejected() const {
    return handler_changed_while_rejected_;
  }

  std::future<Response> Invoke(const std::string& method,
                               EncodableMap arguments = {},
                               std::function<void()> after_reply = {}) {
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
    handler_(message->data(), message->size(),
             [completion, &codec, replies = replies_,
              after_reply = std::move(after_reply)](const uint8_t* reply,
                                                    size_t reply_size) {
               replies->fetch_add(1);
               ResponseResult result(completion);
               if (!codec.DecodeAndProcessResponseEnvelope(reply, reply_size,
                                                           &result)) {
                 Response response;
                 response.kind = ResponseKind::kDecodeFailure;
                 completion->set_value(std::move(response));
               }
               if (after_reply) after_reply();
             });
    return future;
  }

 private:
  std::shared_ptr<std::atomic_int> replies_ =
      std::make_shared<std::atomic_int>(0);
  flutter::BinaryMessageHandler handler_;
  bool reject_handler_changes_ = false;
  bool handler_changed_while_rejected_ = false;
};

struct FakeStoreLicense : implements<FakeStoreLicense, IStoreLicense> {
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

struct FakeAppLicense : implements<FakeAppLicense, IStoreAppLicense> {
  explicit FakeAppLicense(
      Windows::Foundation::Collections::IMapView<hstring, StoreLicense>
          add_on_licenses,
      bool active = true)
      : add_on_licenses_(std::move(add_on_licenses)), active_(active) {}

  hstring SkuStoreId() const { return L"app-sku"; }
  bool IsActive() const { return active_; }
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
  bool active_;
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

class FakeStoreApi : public StoreApi {
 public:
  FakeStoreApi(
      StoreAppLicense app_license,
      StorePurchaseStatus purchase_status = StorePurchaseStatus::Succeeded,
      hresult purchase_error = S_OK, std::chrono::milliseconds delay = 20ms)
      : app_license_(std::move(app_license)),
        purchase_status_(purchase_status),
        purchase_error_(purchase_error),
        delay_(delay) {}

  IAsyncOperation<StorePurchaseResult> RequestPurchaseAsync(
      hstring store_id) override {
    auto cancellation = co_await get_cancellation_token();
    cancellation.callback([this] { cancellations_.fetch_add(1); });
    last_purchase_id_ = std::move(store_id);
    co_await resume_after(delay_);
    auto purchase_interface =
        make<FakePurchaseResult>(purchase_status_, purchase_error_);
    StorePurchaseResult purchase{nullptr};
    attach_abi(purchase, detach_abi(purchase_interface));
    co_return purchase;
  }

  IAsyncOperation<StoreProductQueryResult> GetProductsAsync() override {
    auto cancellation = co_await get_cancellation_token();
    cancellation.callback([this] { cancellations_.fetch_add(1); });
    co_await resume_after(delay_);
    co_return StoreProductQueryResult{nullptr};
  }

  IAsyncOperation<StoreAppLicense> GetAppLicenseAsync() override {
    auto cancellation = co_await get_cancellation_token();
    cancellation.callback([this] { cancellations_.fetch_add(1); });
    co_await resume_after(delay_);
    co_return app_license_;
  }

  IAsyncOperation<StoreRateAndReviewResult> RequestRateAndReviewAppAsync()
      override {
    return nullptr;
  }

  int cancellations() const { return cancellations_.load(); }

  hstring last_purchase_id() const { return last_purchase_id_; }

 private:
  StoreAppLicense app_license_;
  StorePurchaseStatus purchase_status_;
  hresult purchase_error_;
  std::chrono::milliseconds delay_;
  hstring last_purchase_id_;
  std::atomic_int cancellations_{0};
};

class InitiallyFailingStoreApi final : public StoreApi {
 public:
  InitiallyFailingStoreApi(std::shared_ptr<StoreApi> next, bool standard_error)
      : next_(std::move(next)), standard_error_(standard_error) {}

  IAsyncOperation<StorePurchaseResult> RequestPurchaseAsync(
      hstring id) override {
    FailFirstCall();
    return next_->RequestPurchaseAsync(std::move(id));
  }
  IAsyncOperation<StoreProductQueryResult> GetProductsAsync() override {
    FailFirstCall();
    return next_->GetProductsAsync();
  }
  IAsyncOperation<StoreAppLicense> GetAppLicenseAsync() override {
    FailFirstCall();
    return next_->GetAppLicenseAsync();
  }
  IAsyncOperation<StoreRateAndReviewResult> RequestRateAndReviewAppAsync()
      override {
    FailFirstCall();
    return next_->RequestRateAndReviewAppAsync();
  }

 private:
  void FailFirstCall() {
    if (!fail_) return;
    fail_ = false;
    if (standard_error_)
      throw std::runtime_error("Test synchronous Store failure");
    throw hresult_error(E_ACCESSDENIED, L"Test synchronous Store failure");
  }
  std::shared_ptr<StoreApi> next_;
  bool standard_error_;
  bool fail_ = true;
};

class NullOperationStoreApi final : public StoreApi {
 public:
  IAsyncOperation<StorePurchaseResult> RequestPurchaseAsync(hstring) override {
    return nullptr;
  }
  IAsyncOperation<StoreProductQueryResult> GetProductsAsync() override {
    return nullptr;
  }
  IAsyncOperation<StoreAppLicense> GetAppLicenseAsync() override {
    return nullptr;
  }
  IAsyncOperation<StoreRateAndReviewResult> RequestRateAndReviewAppAsync()
      override {
    return nullptr;
  }
};

void PumpMessages() {
  MSG message;
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  MsgWaitForMultipleObjectsEx(0, nullptr, 2, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
}

void PumpFor(std::chrono::milliseconds duration) {
  const auto until = std::chrono::steady_clock::now() + duration;
  while (std::chrono::steady_clock::now() < until) PumpMessages();
}

Response WaitFor(std::future<Response> future) {
  const auto until = std::chrono::steady_clock::now() + 3s;
  while (future.wait_for(0s) != std::future_status::ready) {
    if (std::chrono::steady_clock::now() >= until) {
      Response response;
      response.kind = ResponseKind::kDecodeFailure;
      response.error_code = "timeout";
      return response;
    }
    PumpMessages();
  }
  return future.get();
}

EncodableMap StoreIdArguments(const std::string& store_id) {
  return {{EncodableValue("storeId"), EncodableValue(store_id)}};
}

StoreAppLicense CreateAppLicense(
    const std::string& product_store_id = "test-product",
    DateTime expiration = clock::now() + 1h, bool app_active = true) {
  auto licenses = single_threaded_map<hstring, StoreLicense>();
  auto license_interface =
      make<FakeStoreLicense>(to_hstring(product_store_id + "/sku"), expiration);
  StoreLicense license{nullptr};
  attach_abi(license, detach_abi(license_interface));
  licenses.Insert(L"collection-key", license);

  auto app_license_interface =
      make<FakeAppLicense>(licenses.GetView(), app_active);
  StoreAppLicense app_license{nullptr};
  attach_abi(app_license, detach_abi(app_license_interface));
  return app_license;
}

struct TestWindow {
  HWND root = nullptr;
  HWND child = nullptr;

  TestWindow() {
    root =
        CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 100, 100,
                        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (root != nullptr) {
      child = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 50, 50, root,
                              nullptr, GetModuleHandleW(nullptr), nullptr);
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

#define TEST_CHECK(condition)                                  \
  do {                                                         \
    if (!(condition)) {                                        \
      std::cerr << __FUNCTION__ << ":" << __LINE__             \
                << ": check failed: " #condition << std::endl; \
      return false;                                            \
    }                                                          \
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

bool PluginDestructionDoesNotTouchMessenger() {
  TestBinaryMessenger messenger;
  {
    WindowsIapPlugin plugin(
        &messenger, [] { return static_cast<HWND>(nullptr); },
        [](HWND) { return std::shared_ptr<StoreApi>(); });
    messenger.RejectFurtherHandlerChanges();
  }

  TEST_CHECK(!messenger.handler_changed_while_rejected());
  return true;
}

bool ShutdownCompletesPendingStoreRequests() {
  auto store = std::make_shared<FakeStoreApi>(
      CreateAppLicense(), StorePurchaseStatus::Succeeded, S_OK, 120ms);
  TestWindow window;
  TEST_CHECK(window.child != nullptr);
  TestBinaryMessenger messenger;
  std::future<Response> query;
  std::future<Response> purchase;
  {
    WindowsIapPlugin plugin(
        &messenger, [&window] { return window.child; },
        [store](HWND) { return store; });
    query = messenger.Invoke("getAppLicense");
    purchase =
        messenger.Invoke("makePurchase", StoreIdArguments("test-product"));
  }
  const auto query_response = WaitFor(std::move(query));
  const auto purchase_response = WaitFor(std::move(purchase));
  TEST_CHECK(query_response.kind == ResponseKind::kError);
  TEST_CHECK(query_response.error_code == "plugin_disposed");
  TEST_CHECK(purchase_response.kind == ResponseKind::kError);
  TEST_CHECK(purchase_response.error_code == "plugin_disposed");
  TEST_CHECK(store->cancellations() == 2);
  PumpFor(200ms);
  TEST_CHECK(messenger.response_count() == 2);
  return true;
}

bool StaleMethodHandlerDoesNotAccessDestroyedPlugin() {
  TestWindow window;
  TestBinaryMessenger messenger;
  int factories = 0;
  {
    WindowsIapPlugin plugin(
        &messenger, [&window] { return window.child; },
        [&factories](HWND) {
          ++factories;
          return std::shared_ptr<StoreApi>();
        });
  }
  const auto response = WaitFor(messenger.Invoke("getProducts"));
  TEST_CHECK(response.kind == ResponseKind::kError);
  TEST_CHECK(response.error_code == "plugin_disposed");
  TEST_CHECK(factories == 0);
  return true;
}

bool EveryQueryTimesOutAndCancelsWithoutDuplicateReplies() {
  const char* methods[] = {"getProducts", "getAppLicense", "getAddonLicenses",
                           "checkPurchase"};
  for (const auto* method : methods) {
    auto store = std::make_shared<FakeStoreApi>(
        CreateAppLicense(), StorePurchaseStatus::Succeeded, S_OK, 120ms);
    TestWindow window;
    TestBinaryMessenger messenger;
    WindowsIapPlugin plugin(
        &messenger, [&window] { return window.child; },
        [store](HWND) { return store; }, 20ms);
    const auto response = WaitFor(messenger.Invoke(method));
    TEST_CHECK(response.kind == ResponseKind::kError);
    TEST_CHECK(response.error_code == "store_timeout");
    // The timeout reply may arrive just before Cancel finishes on the timer
    // thread.
    PumpFor(200ms);
    TEST_CHECK(store->cancellations() == 1);
    TEST_CHECK(messenger.response_count() == 1);
  }
  return true;
}

bool DelayedPurchaseKeepsItsGuardBeyondQueryDeadline() {
  auto store = std::make_shared<FakeStoreApi>(
      CreateAppLicense(), StorePurchaseStatus::Succeeded, S_OK, 150ms);
  TestWindow window;
  TestBinaryMessenger messenger;
  WindowsIapPlugin plugin(
      &messenger, [&window] { return window.child; },
      [store](HWND) { return store; }, 20ms);
  auto first =
      messenger.Invoke("makePurchase", StoreIdArguments("test-product"));
  PumpFor(50ms);
  const auto second = WaitFor(
      messenger.Invoke("makePurchase", StoreIdArguments("test-product")));
  TEST_CHECK(second.error_code == "purchase_in_progress");
  TEST_CHECK(WaitFor(std::move(first)).kind == ResponseKind::kSuccess);
  TEST_CHECK(store->cancellations() == 0);
  return true;
}

bool TimeoutCompletionRaceRepliesExactlyOnce() {
  TestWindow window;
  for (int index = 0; index < 24; ++index) {
    auto store = std::make_shared<FakeStoreApi>(
        CreateAppLicense(), StorePurchaseStatus::Succeeded, S_OK, 5ms);
    TestBinaryMessenger messenger;
    {
      WindowsIapPlugin plugin(
          &messenger, [&window] { return window.child; },
          [store](HWND) { return store; }, 5ms);
      const auto response = WaitFor(messenger.Invoke("getAppLicense"));
      TEST_CHECK(response.kind == ResponseKind::kSuccess ||
                 response.error_code == "store_timeout");
    }
    PumpFor(10ms);
    TEST_CHECK(messenger.response_count() == 1);
  }
  return true;
}

bool StoreInitializationFailurePreservesReplyAndPurchaseGuard() {
  auto store = std::make_shared<FakeStoreApi>(CreateAppLicense());
  TestWindow window;
  TestBinaryMessenger messenger;
  bool fail_once = true;
  WindowsIapPlugin plugin(
      &messenger, [&window] { return window.child; },
      [store, &fail_once](HWND) -> std::shared_ptr<StoreApi> {
        if (fail_once) {
          fail_once = false;
          throw hresult_error(E_FAIL, L"Test Store initialization failure.");
        }
        return store;
      });
  const auto failed = WaitFor(
      messenger.Invoke("makePurchase", StoreIdArguments("test-product")));
  TEST_CHECK(failed.kind == ResponseKind::kError);
  TEST_CHECK(failed.error_code == "0x80004005");
  const auto retried = WaitFor(
      messenger.Invoke("makePurchase", StoreIdArguments("test-product")));
  TEST_CHECK(retried.kind == ResponseKind::kSuccess);
  return true;
}

bool NullAsyncOperationsReturnErrorsInsteadOfDereferencingNull() {
  auto store = std::make_shared<NullOperationStoreApi>();
  TestWindow window;
  TestBinaryMessenger messenger;
  WindowsIapPlugin plugin(
      &messenger, [&window] { return window.child; },
      [store](HWND) { return store; });
  const char* methods[] = {"getProducts",      "getAppLicense",
                           "getAddonLicenses", "checkPurchase",
                           "makePurchase",     "makePurchaseDetailed"};
  for (const auto* method : methods) {
    const auto response =
        WaitFor(messenger.Invoke(method, StoreIdArguments("test-product")));
    TEST_CHECK(response.kind == ResponseKind::kError);
    TEST_CHECK(response.error_code == "0x8000FFFF");
  }
  return true;
}

bool SynchronousStoreErrorsReplyAndAllowPurchaseRetry() {
  for (const bool standard_error : {false, true}) {
    const char* methods[] = {"getProducts",      "getAppLicense",
                             "getAddonLicenses", "checkPurchase",
                             "makePurchase",     "makePurchaseDetailed"};
    for (const auto* method : methods) {
      auto next = std::make_shared<FakeStoreApi>(CreateAppLicense());
      auto store =
          std::make_shared<InitiallyFailingStoreApi>(next, standard_error);
      TestWindow window;
      TestBinaryMessenger messenger;
      WindowsIapPlugin plugin(
          &messenger, [&window] { return window.child; },
          [store](HWND) { return store; });
      const auto failed =
          WaitFor(messenger.Invoke(method, StoreIdArguments("test-product")));
      TEST_CHECK(failed.kind == ResponseKind::kError);
      TEST_CHECK(failed.error_code ==
                 (standard_error ? "native_error" : "0x80070005"));
      const auto retried = WaitFor(
          messenger.Invoke("makePurchase", StoreIdArguments("test-product")));
      TEST_CHECK(retried.kind == ResponseKind::kSuccess);
      TEST_CHECK(messenger.response_count() == 2);
    }
  }
  return true;
}

bool AllNativePurchaseStatusesRoundTrip() {
  for (int32_t status = 0; status <= 4; ++status) {
    const hresult extended_error =
        status >= 3 ? hresult{E_FAIL} : hresult{S_OK};
    auto store = std::make_shared<FakeStoreApi>(
        CreateAppLicense(), static_cast<StorePurchaseStatus>(status),
        extended_error, 3ms);
    TestWindow window;
    TestBinaryMessenger messenger;
    WindowsIapPlugin plugin(
        &messenger, [&window] { return window.child; },
        [store](HWND) { return store; });
    const auto legacy = WaitFor(
        messenger.Invoke("makePurchase", StoreIdArguments("test-product")));
    TEST_CHECK(legacy.kind == ResponseKind::kSuccess);
    TEST_CHECK(std::get<int32_t>(legacy.value) == status);
    const auto detailed = WaitFor(messenger.Invoke(
        "makePurchaseDetailed", StoreIdArguments("test-product")));
    TEST_CHECK(detailed.kind == ResponseKind::kSuccess);
    const auto& details = std::get<EncodableMap>(detailed.value);
    TEST_CHECK(std::get<int32_t>(details.at(EncodableValue("status"))) ==
               status);
    const auto& error = details.at(EncodableValue("extendedErrorCode"));
    if (status >= 3) {
      TEST_CHECK(std::get<std::string>(error) == "0x80004005");
    } else {
      TEST_CHECK(std::holds_alternative<std::monostate>(error));
    }
  }
  return true;
}

bool InactiveExpiredAndUnrelatedLicensesDoNotGrantAccess() {
  const StoreAppLicense licenses[] = {
      CreateAppLicense("test-product", clock::now() + 1h, false),
      CreateAppLicense("test-product", clock::now() - 1s),
      CreateAppLicense("other-product"),
  };
  for (const auto& license : licenses) {
    auto store = std::make_shared<FakeStoreApi>(license);
    TestWindow window;
    TestBinaryMessenger messenger;
    WindowsIapPlugin plugin(
        &messenger, [&window] { return window.child; },
        [store](HWND) { return store; });
    const auto response = WaitFor(
        messenger.Invoke("checkPurchase", StoreIdArguments("test-product")));
    TEST_CHECK(response.kind == ResponseKind::kSuccess);
    TEST_CHECK(!std::get<bool>(response.value));
  }
  return true;
}

bool WrongThreadDoesNotCreateStoreOrUseView() {
  TestBinaryMessenger messenger;
  std::atomic_bool touched_view{false};
  std::atomic_bool created_store{false};
  WindowsIapPlugin plugin(
      &messenger,
      [&touched_view] {
        touched_view.store(true);
        return HWND{nullptr};
      },
      [&created_store](HWND) {
        created_store.store(true);
        return std::shared_ptr<StoreApi>();
      });
  auto worker = std::async(std::launch::async, [&messenger] {
    init_apartment(apartment_type::multi_threaded);
    auto response = WaitFor(messenger.Invoke("getProducts"));
    uninit_apartment();
    return response;
  });
  const auto response = worker.get();
  TEST_CHECK(response.kind == ResponseKind::kError);
  TEST_CHECK(response.error_code == "0x8001010E");
  TEST_CHECK(!touched_view.load());
  TEST_CHECK(!created_store.load());
  return true;
}

bool TimedOutQueryDoesNotPoisonTheNextRequest() {
  auto slow = std::make_shared<FakeStoreApi>(
      CreateAppLicense(), StorePurchaseStatus::Succeeded, S_OK, 120ms);
  auto fast = std::make_shared<FakeStoreApi>(
      CreateAppLicense(), StorePurchaseStatus::Succeeded, S_OK, 3ms);
  TestWindow window;
  TestBinaryMessenger messenger;
  bool first = true;
  WindowsIapPlugin plugin(
      &messenger, [&window] { return window.child; },
      [&](HWND) -> std::shared_ptr<StoreApi> {
        if (first) {
          first = false;
          return slow;
        }
        return fast;
      },
      20ms);
  const auto timed_out = WaitFor(messenger.Invoke("getAppLicense"));
  TEST_CHECK(timed_out.error_code == "store_timeout");
  const auto retried = WaitFor(messenger.Invoke("getAppLicense"));
  TEST_CHECK(retried.kind == ResponseKind::kSuccess);
  PumpFor(200ms);
  TEST_CHECK(messenger.response_count() == 2);
  TEST_CHECK(slow->cancellations() == 1);
  TEST_CHECK(fast->cancellations() == 0);
  return true;
}

Response ReadCatalog(CatalogShape shape) {
  auto store = std::make_shared<CatalogStoreApi>(shape);
  TestWindow window;
  TestBinaryMessenger messenger;
  WindowsIapPlugin plugin(
      &messenger, [&window] { return window.child; },
      [store](HWND) { return store; });
  return WaitFor(messenger.Invoke("getProducts"));
}

bool CatalogSerializesNestedPricesAndSubscriptionInfo() {
  const auto response = ReadCatalog(CatalogShape::kComplete);
  TEST_CHECK(response.kind == ResponseKind::kSuccess);
  const auto& products = std::get<flutter::EncodableList>(response.value);
  TEST_CHECK(products.size() == 1);
  const auto& product = std::get<EncodableMap>(products.front());
  TEST_CHECK(std::get<std::string>(product.at(EncodableValue("price"))) ==
             "$3.00");
  const auto& skus =
      std::get<flutter::EncodableList>(product.at(EncodableValue("skus")));
  TEST_CHECK(skus.size() == 1);
  const auto& sku = std::get<EncodableMap>(skus.front());
  const auto& price = std::get<EncodableMap>(sku.at(EncodableValue("price")));
  TEST_CHECK(std::get<std::string>(price.at(EncodableValue("currencyCode"))) ==
             "USD");
  TEST_CHECK(std::get<std::string>(
                 price.at(EncodableValue("unformattedPrice"))) == "3.00");
  const auto& subscription =
      std::get<EncodableMap>(sku.at(EncodableValue("subscriptionInfo")));
  TEST_CHECK(
      std::get<int64_t>(subscription.at(EncodableValue("billingPeriod"))) == 1);
  const auto& collection =
      std::get<EncodableMap>(sku.at(EncodableValue("collectionData")));
  TEST_CHECK(std::get<bool>(collection.at(EncodableValue("isTrial"))));
  const auto& availabilities = std::get<flutter::EncodableList>(
      sku.at(EncodableValue("availabilities")));
  TEST_CHECK(availabilities.size() == 1);
  const auto& availability = std::get<EncodableMap>(availabilities.front());
  const auto& offer_price =
      std::get<EncodableMap>(availability.at(EncodableValue("price")));
  TEST_CHECK(std::get<std::string>(
                 offer_price.at(EncodableValue("formattedPrice"))) == "$3.00");
  return true;
}

bool MissingCatalogPricesReturnErrors() {
  for (const auto shape :
       {CatalogShape::kNoSkuPrice, CatalogShape::kNoAvailabilityPrice}) {
    const auto response = ReadCatalog(shape);
    TEST_CHECK(response.kind == ResponseKind::kError);
    TEST_CHECK(response.error_code == "0x8000FFFF");
  }
  return true;
}

bool MissingCatalogCollectionsReturnErrors() {
  for (const auto shape :
       {CatalogShape::kNoSkus, CatalogShape::kNoAvailabilities}) {
    const auto response = ReadCatalog(shape);
    TEST_CHECK(response.kind == ResponseKind::kError);
    TEST_CHECK(response.error_code == "0x8000FFFF");
  }
  return true;
}

bool EmptyCatalogCollectionsRemainValid() {
  for (const auto shape :
       {CatalogShape::kEmptySkus, CatalogShape::kEmptyAvailabilities}) {
    const auto response = ReadCatalog(shape);
    TEST_CHECK(response.kind == ResponseKind::kSuccess);
  }
  return true;
}

bool CatalogQueryErrorsPreserveHResult() {
  const auto response = ReadCatalog(CatalogShape::kQueryError);
  TEST_CHECK(response.kind == ResponseKind::kError);
  TEST_CHECK(response.error_code == "0x80070005");
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

    const Response first = WaitFor(first_messenger.Invoke("getAppLicense"));
    const Response second = WaitFor(second_messenger.Invoke("getAppLicense"));
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
      CreateAppLicense(product_store_id), StorePurchaseStatus::Succeeded, S_OK,
      75ms);
  TestWindow window;
  TEST_CHECK(window.child != nullptr);
  TestBinaryMessenger messenger;
  WindowsIapPlugin plugin(
      &messenger, [&window] { return window.child; },
      [store](HWND) { return store; });

  const Response response = WaitFor(
      messenger.Invoke("checkPurchase", StoreIdArguments(product_store_id)));
  TEST_CHECK(response.kind == ResponseKind::kSuccess);
  const auto* entitled = std::get_if<bool>(&response.value);
  TEST_CHECK(entitled != nullptr && *entitled);
  return true;
}

bool PurchaseStatusAndExtendedErrorAreBothAvailable() {
  auto store = std::make_shared<FakeStoreApi>(CreateAppLicense(),
                                              StorePurchaseStatus::NetworkError,
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

  const Response detailed = WaitFor(messenger.Invoke(
      "makePurchaseDetailed", StoreIdArguments("test-product")));
  TEST_CHECK(detailed.kind == ResponseKind::kSuccess);
  const auto* details = std::get_if<EncodableMap>(&detailed.value);
  TEST_CHECK(details != nullptr);
  const auto status_entry = details->find(EncodableValue("status"));
  const auto error_entry = details->find(EncodableValue("extendedErrorCode"));
  TEST_CHECK(status_entry != details->end());
  TEST_CHECK(error_entry != details->end());
  const auto* status = std::get_if<int32_t>(&status_entry->second);
  const auto* extended_error = std::get_if<std::string>(&error_entry->second);
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

struct FakeReviewResult
    : implements<FakeReviewResult, IStoreRateAndReviewResult> {
  FakeReviewResult(StoreRateAndReviewStatus status, hstring json, hresult error,
                   bool was_updated, hresult getter_error)
      : status_(status),
        json_(std::move(json)),
        error_(error),
        was_updated_(was_updated),
        getter_error_(getter_error) {}
  StoreRateAndReviewStatus Status() const { return status_; }
  hstring ExtendedJsonData() const {
    check_hresult(getter_error_);
    return json_;
  }
  hresult ExtendedError() const { return error_; }
  bool WasUpdated() const { return was_updated_; }

 private:
  StoreRateAndReviewStatus status_;
  hstring json_;
  hresult error_;
  bool was_updated_;
  hresult getter_error_;
};

class FakeReviewStoreApi final : public FakeStoreApi {
 public:
  explicit FakeReviewStoreApi(std::chrono::milliseconds delay = 20ms)
      : FakeStoreApi(CreateAppLicense(), StorePurchaseStatus::Succeeded, S_OK,
                     delay),
        delay_(delay) {}

  StoreRateAndReviewStatus status = StoreRateAndReviewStatus::Succeeded;
  hstring json = LR"({"status":"success","data":{"updated":false}})";
  hresult error = S_OK;
  bool was_updated = true;
  bool null_result = false;
  bool complete_synchronously = false;
  hresult async_error = S_OK;
  hresult getter_error = S_OK;
  std::shared_ptr<StoreApi> license_api;
  std::shared_ptr<StoreApi> catalog_api;
  int starts = 0;
  DWORD start_thread = 0;
  std::atomic_int review_cancellations{0};
  IAsyncOperation<StoreRateAndReviewResult> operation{nullptr};

  IAsyncOperation<StoreRateAndReviewResult> RequestRateAndReviewAppAsync()
      override {
    ++starts;
    start_thread = GetCurrentThreadId();
    operation = ReviewAsync();
    return operation;
  }

  IAsyncOperation<StoreAppLicense> GetAppLicenseAsync() override {
    return license_api ? license_api->GetAppLicenseAsync()
                       : FakeStoreApi::GetAppLicenseAsync();
  }

  IAsyncOperation<StoreProductQueryResult> GetProductsAsync() override {
    return catalog_api ? catalog_api->GetProductsAsync()
                       : FakeStoreApi::GetProductsAsync();
  }

 private:
  IAsyncOperation<StoreRateAndReviewResult> ReviewAsync() {
    auto cancellation = co_await get_cancellation_token();
    cancellation.callback([this] { review_cancellations.fetch_add(1); });
    if (!complete_synchronously) co_await resume_after(delay_);
    check_hresult(async_error);
    if (null_result) co_return StoreRateAndReviewResult{nullptr};
    co_return windows_iap::testing::MakeStoreObject<StoreRateAndReviewResult,
                                                    FakeReviewResult>(
        status, json, error, was_updated, getter_error);
  }
  std::chrono::milliseconds delay_;
};

bool ReviewResultsPreserveStatusesAndJsonSemantics() {
  struct Case {
    StoreRateAndReviewStatus status;
    const wchar_t* json;
    std::optional<bool> updated;
    bool was_updated;
  };
  const Case cases[] = {
      {StoreRateAndReviewStatus::Succeeded,
       LR"({"status":"success","data":{"updated":false}})", false, true},
      {StoreRateAndReviewStatus::Succeeded,
       LR"({"status":"success","data":{"updated":true}})", true, false},
      {StoreRateAndReviewStatus::CanceledByUser,
       LR"({"status":"success","data":{"updated":true}})",
       {},
       true},
      {StoreRateAndReviewStatus::NetworkError, L"", {}, false},
      {StoreRateAndReviewStatus::Error, L"{}", {}, false},
      {StoreRateAndReviewStatus::Succeeded, L"", {}, true},
      {StoreRateAndReviewStatus::Succeeded, L"{invalid", {}, true},
      {StoreRateAndReviewStatus::Succeeded, L"null", {}, true},
      {StoreRateAndReviewStatus::Succeeded, L"[]", {}, true},
      {StoreRateAndReviewStatus::Succeeded, L"{}", {}, true},
      {StoreRateAndReviewStatus::Succeeded,
       LR"({"status":"aborted","data":{"updated":false}})",
       {},
       true},
      {StoreRateAndReviewStatus::Succeeded,
       LR"({"status":"success","data":{"updated":"false"}})",
       {},
       true},
      {StoreRateAndReviewStatus::Succeeded,
       LR"({"status":"success","data":null})",
       {},
       true},
      {StoreRateAndReviewStatus::Succeeded,
       LR"({"status":"success","data":{}})",
       {},
       true},
  };
  TestWindow window;
  for (const auto& item : cases) {
    auto store = std::make_shared<FakeReviewStoreApi>(3ms);
    store->status = item.status;
    store->json = item.json;
    store->was_updated = item.was_updated;
    store->error = item.status == StoreRateAndReviewStatus::NetworkError
                       ? hresult{static_cast<int32_t>(0x80072EE7)}
                   : item.status == StoreRateAndReviewStatus::Error
                       ? hresult{E_FAIL}
                       : hresult{S_OK};
    TestBinaryMessenger messenger;
    WindowsIapPlugin plugin(
        &messenger, [&window] { return window.child; },
        [store](HWND hwnd) {
          if (hwnd == nullptr) throw hresult_error(E_FAIL);
          return store;
        });
    const auto response = WaitFor(messenger.Invoke("requestReview"));
    if (response.kind != ResponseKind::kSuccess) {
      std::cerr << "Review failure: " << response.error_code << " "
                << response.error_message << " json=" << to_string(item.json)
                << std::endl;
    }
    TEST_CHECK(response.kind == ResponseKind::kSuccess);
    const auto& result = std::get<EncodableMap>(response.value);
    TEST_CHECK(std::get<int32_t>(result.at(EncodableValue("status"))) ==
               static_cast<int32_t>(item.status));
    TEST_CHECK(std::get<std::string>(result.at(EncodableValue(
                   "extendedJsonData"))) == to_string(item.json));
    TEST_CHECK(std::get<bool>(result.at(EncodableValue("wasUpdated"))) ==
               item.was_updated);
    const auto& updated = result.at(EncodableValue("updatedExistingReview"));
    if (item.updated.has_value()) {
      TEST_CHECK(std::get<bool>(updated) == *item.updated);
    } else {
      TEST_CHECK(std::holds_alternative<std::monostate>(updated));
    }
    const auto& error = result.at(EncodableValue("extendedErrorCode"));
    if (store->error.value == S_OK) {
      TEST_CHECK(std::holds_alternative<std::monostate>(error));
    } else {
      TEST_CHECK(std::get<std::string>(error) ==
                 (item.status == StoreRateAndReviewStatus::NetworkError
                      ? "0x80072EE7"
                      : "0x80004005"));
    }
    TEST_CHECK(store->start_thread == GetCurrentThreadId());
    // Every completion releases the guard and permits another dialog.
    TEST_CHECK(WaitFor(messenger.Invoke("requestReview")).kind ==
               ResponseKind::kSuccess);
  }
  return true;
}

bool ReviewAndPurchaseDialogsAreMutuallyExclusiveWithoutQueryTimeout() {
  TestWindow window;
  for (const auto* purchase : {"makePurchase", "makePurchaseDetailed"}) {
    auto store = std::make_shared<FakeReviewStoreApi>(120ms);
    TestBinaryMessenger messenger;
    WindowsIapPlugin plugin(
        &messenger, [&window] { return window.child; },
        [store](HWND) { return store; }, 5ms);
    auto review = messenger.Invoke("requestReview");
    PumpFor(20ms);
    TEST_CHECK(review.wait_for(0s) != std::future_status::ready);
    TEST_CHECK(WaitFor(messenger.Invoke("requestReview")).error_code ==
               "review_in_progress");
    TEST_CHECK(WaitFor(messenger.Invoke(purchase, StoreIdArguments("test")))
                   .error_code == "review_in_progress");
    TEST_CHECK(store->starts == 1);
    TEST_CHECK(WaitFor(std::move(review)).kind == ResponseKind::kSuccess);
    auto payment = messenger.Invoke(purchase, StoreIdArguments("test"));
    TEST_CHECK(WaitFor(messenger.Invoke("requestReview")).error_code ==
               "purchase_in_progress");
    TEST_CHECK(WaitFor(std::move(payment)).kind == ResponseKind::kSuccess);
    TEST_CHECK(WaitFor(messenger.Invoke("requestReview")).kind ==
               ResponseKind::kSuccess);
    TEST_CHECK(store->review_cancellations.load() == 0);
    TEST_CHECK(store->cancellations() == 0);
  }
  return true;
}

bool ReviewShutdownCancelsAndDiscardsLateReplies() {
  auto store = std::make_shared<FakeReviewStoreApi>(120ms);
  TestWindow window;
  TestBinaryMessenger messenger;
  std::future<Response> review;
  {
    WindowsIapPlugin plugin(
        &messenger, [&window] { return window.child; },
        [store](HWND) { return store; });
    review = messenger.Invoke("requestReview");
    DestroyWindow(window.child);
    window.child = nullptr;
    messenger.RejectFurtherHandlerChanges();
  }
  TEST_CHECK(WaitFor(std::move(review)).error_code == "plugin_disposed");
  TEST_CHECK(store->review_cancellations.load() == 1);
  PumpFor(200ms);
  TEST_CHECK(messenger.response_count() == 1);
  TEST_CHECK(!messenger.handler_changed_while_rejected());
  TEST_CHECK(WaitFor(messenger.Invoke("requestReview")).error_code ==
             "plugin_disposed");
  return true;
}

bool CanceledNativeReviewAllowsRetry() {
  auto store = std::make_shared<FakeReviewStoreApi>(60ms);
  TestWindow window;
  TestBinaryMessenger messenger;
  WindowsIapPlugin plugin(
      &messenger, [&window] { return window.child; },
      [store](HWND) { return store; });
  auto review = messenger.Invoke("requestReview");
  store->operation.Cancel();
  TEST_CHECK(WaitFor(std::move(review)).error_code == "0x800704C7");
  TEST_CHECK(WaitFor(messenger.Invoke("requestReview")).kind ==
             ResponseKind::kSuccess);
  TEST_CHECK(messenger.response_count() == 2);
  return true;
}

bool ReviewFailuresReleaseDialogGuard() {
  TestWindow window;
  auto good = std::make_shared<FakeReviewStoreApi>(3ms);
  for (int failure = 0; failure < 6; ++failure) {
    std::shared_ptr<StoreApi> failed;
    if (failure < 2) {
      failed = std::make_shared<InitiallyFailingStoreApi>(good, failure == 1);
    } else if (failure == 2) {
      failed = std::make_shared<NullOperationStoreApi>();
    } else if (failure == 3) {
      auto null_store = std::make_shared<FakeReviewStoreApi>(3ms);
      null_store->null_result = true;
      failed = null_store;
    }
    int factories = 0;
    TestBinaryMessenger messenger;
    WindowsIapPlugin plugin(
        &messenger, [&window] { return window.child; },
        [&](HWND) -> std::shared_ptr<StoreApi> {
          if (++factories == 1) {
            if (failure == 4) throw hresult_error(E_NOTIMPL);
            if (failure == 5) throw std::runtime_error("Initialization failed");
            return failed;
          }
          return good;
        });
    const auto response = WaitFor(messenger.Invoke("requestReview"));
    TEST_CHECK(response.kind == ResponseKind::kError);
    const char* errors[] = {"0x80070005", "native_error", "0x8000FFFF",
                            "0x8000FFFF", "0x80004001",   "native_error"};
    TEST_CHECK(response.error_code == errors[failure]);
    TEST_CHECK(WaitFor(messenger.Invoke("requestReview")).kind ==
               ResponseKind::kSuccess);
    TEST_CHECK(
        WaitFor(messenger.Invoke("makePurchase", StoreIdArguments("test")))
            .kind == ResponseKind::kSuccess);
  }
  return true;
}

bool ReviewRejectsInvalidWindowAndWrongThreadBeforeStartingStore() {
  TestWindow window;
  TestBinaryMessenger messenger;
  bool touched_view = false;
  bool created_store = false;
  WindowsIapPlugin plugin(
      &messenger,
      [&] {
        touched_view = true;
        return window.child;
      },
      [&](HWND) {
        created_store = true;
        return std::shared_ptr<StoreApi>();
      });
  const auto response = std::async(std::launch::async, [&messenger] {
                          init_apartment(apartment_type::multi_threaded);
                          auto response =
                              WaitFor(messenger.Invoke("requestReview"));
                          uninit_apartment();
                          return response;
                        }).get();
  TEST_CHECK(response.error_code == "0x8001010E");
  TEST_CHECK(!touched_view && !created_store);
  DestroyWindow(window.child);
  window.child = nullptr;
  TEST_CHECK(WaitFor(messenger.Invoke("requestReview")).error_code ==
             "0x80070578");
  TEST_CHECK(touched_view && !created_store);
  return true;
}

bool ReviewDoesNotChangeCatalogOrSubscriptionQueries() {
  TestWindow window;
  for (const auto status : {StoreRateAndReviewStatus::Succeeded,
                            StoreRateAndReviewStatus::CanceledByUser,
                            StoreRateAndReviewStatus::NetworkError,
                            StoreRateAndReviewStatus::Error}) {
    auto store = std::make_shared<FakeReviewStoreApi>(300ms);
    store->status = status;
    store->license_api = std::make_shared<FakeStoreApi>(
        CreateAppLicense(), StorePurchaseStatus::Succeeded, S_OK, 3ms);
    store->catalog_api =
        std::make_shared<CatalogStoreApi>(CatalogShape::kComplete);
    TestBinaryMessenger messenger;
    WindowsIapPlugin plugin(
        &messenger, [&window] { return window.child; },
        [store](HWND) { return store; });
    auto review = messenger.Invoke("requestReview");
    for (const auto* method : {"checkPurchase", "getAddonLicenses",
                               "getAppLicense", "getProducts"}) {
      const auto response =
          WaitFor(messenger.Invoke(method, StoreIdArguments("test-product")));
      TEST_CHECK(response.kind == ResponseKind::kSuccess);
      const std::string name(method);
      if (name == "checkPurchase") {
        TEST_CHECK(std::get<bool>(response.value));
      } else if (name == "getProducts") {
        const auto& products = std::get<flutter::EncodableList>(response.value);
        TEST_CHECK(products.size() == 1);
        const auto& product = std::get<EncodableMap>(products.front());
        TEST_CHECK(std::get<std::string>(product.at(EncodableValue("price"))) ==
                   "$3.00");
      } else {
        const auto& value = std::get<EncodableMap>(response.value);
        const auto& licenses = name == "getAppLicense"
                                   ? std::get<EncodableMap>(value.at(
                                         EncodableValue("addOnLicenses")))
                                   : value;
        TEST_CHECK(licenses.size() == 1);
        const auto& license = std::get<EncodableMap>(licenses.begin()->second);
        TEST_CHECK(std::get<bool>(license.at(EncodableValue("isEntitled"))));
        TEST_CHECK(std::get<std::string>(license.at(
                       EncodableValue("productStoreId"))) == "test-product");
      }
    }
    TEST_CHECK(review.wait_for(0s) != std::future_status::ready);
    TEST_CHECK(WaitFor(std::move(review)).kind == ResponseKind::kSuccess);
    TEST_CHECK(std::get<bool>(
        WaitFor(
            messenger.Invoke("checkPurchase", StoreIdArguments("test-product")))
            .value));
    TEST_CHECK(WaitFor(messenger.Invoke("makePurchaseDetailed",
                                        StoreIdArguments("test-product")))
                   .kind == ResponseKind::kSuccess);
  }
  return true;
}

bool QueryTimeoutDoesNotReleasePendingReview() {
  TestWindow window;
  auto store = std::make_shared<FakeReviewStoreApi>(200ms);
  auto slow = std::make_shared<FakeStoreApi>(
      CreateAppLicense(), StorePurchaseStatus::Succeeded, S_OK, 120ms);
  store->license_api = slow;
  TestBinaryMessenger messenger;
  WindowsIapPlugin plugin(
      &messenger, [&window] { return window.child; },
      [store](HWND) { return store; }, 10ms);
  auto review = messenger.Invoke("requestReview");
  TEST_CHECK(WaitFor(messenger.Invoke("getAppLicense")).error_code ==
             "store_timeout");
  TEST_CHECK(WaitFor(messenger.Invoke("requestReview")).error_code ==
             "review_in_progress");
  TEST_CHECK(WaitFor(messenger.Invoke("makePurchase",
                                      StoreIdArguments("test-product")))
                 .error_code == "review_in_progress");
  TEST_CHECK(WaitFor(std::move(review)).kind == ResponseKind::kSuccess);
  TEST_CHECK(store->review_cancellations.load() == 0);
  TEST_CHECK(slow->cancellations() == 1);
  TEST_CHECK(WaitFor(messenger.Invoke("makePurchase",
                                      StoreIdArguments("test-product")))
                 .kind == ResponseKind::kSuccess);
  return true;
}

bool InvalidPurchaseAttemptDoesNotReleasePendingReview() {
  TestWindow window;
  auto store = std::make_shared<FakeReviewStoreApi>(100ms);
  TestBinaryMessenger messenger;
  WindowsIapPlugin plugin(
      &messenger, [&window] { return window.child; },
      [store](HWND) { return store; });
  auto review = messenger.Invoke("requestReview");
  for (const auto* method : {"makePurchase", "makePurchaseDetailed"}) {
    TEST_CHECK(WaitFor(messenger.Invoke(method)).error_code ==
               "invalid_arguments");
  }
  TEST_CHECK(WaitFor(messenger.Invoke("requestReview")).error_code ==
             "review_in_progress");
  TEST_CHECK(WaitFor(std::move(review)).kind == ResponseKind::kSuccess);
  return true;
}

bool AsyncReviewFailuresAllowSubsequentPurchase() {
  TestWindow window;
  for (const bool getter_failure : {false, true}) {
    auto store = std::make_shared<FakeReviewStoreApi>(3ms);
    if (getter_failure) {
      store->getter_error = E_FAIL;
    } else {
      store->async_error = E_ACCESSDENIED;
    }
    TestBinaryMessenger messenger;
    WindowsIapPlugin plugin(
        &messenger, [&window] { return window.child; },
        [store](HWND) { return store; });
    TEST_CHECK(WaitFor(messenger.Invoke("requestReview")).error_code ==
               (getter_failure ? "0x80004005" : "0x80070005"));
    TEST_CHECK(WaitFor(messenger.Invoke("makePurchaseDetailed",
                                        StoreIdArguments("test-product")))
                   .kind == ResponseKind::kSuccess);
    store->getter_error = S_OK;
    store->async_error = S_OK;
    TEST_CHECK(WaitFor(messenger.Invoke("requestReview")).kind ==
               ResponseKind::kSuccess);
    TEST_CHECK(messenger.response_count() == 3);
  }
  return true;
}

bool SynchronousReviewCompletionAllowsImmediateRetry() {
  TestWindow window;
  auto store = std::make_shared<FakeReviewStoreApi>(3ms);
  store->complete_synchronously = true;
  TestBinaryMessenger messenger;
  WindowsIapPlugin plugin(
      &messenger, [&window] { return window.child; },
      [store](HWND) { return store; });
  for (int index = 0; index < 20; ++index) {
    TEST_CHECK(WaitFor(messenger.Invoke("requestReview")).kind ==
               ResponseKind::kSuccess);
  }
  TEST_CHECK(WaitFor(messenger.Invoke("makePurchase",
                                      StoreIdArguments("test-product")))
                 .kind == ResponseKind::kSuccess);
  TEST_CHECK(messenger.response_count() == 21);
  return true;
}

bool ReviewReplyCanImmediatelyStartPurchaseWithoutClearingNewGuard() {
  TestWindow window;
  auto store = std::make_shared<FakeReviewStoreApi>(120ms);
  store->complete_synchronously = true;
  TestBinaryMessenger messenger;
  WindowsIapPlugin plugin(
      &messenger, [&window] { return window.child; },
      [store](HWND) { return store; });
  std::future<Response> purchase;
  auto review = messenger.Invoke("requestReview", {}, [&] {
    purchase = messenger.Invoke("makePurchaseDetailed",
                                StoreIdArguments("test-product"));
  });
  TEST_CHECK(WaitFor(std::move(review)).kind == ResponseKind::kSuccess);
  TEST_CHECK(purchase.valid());
  TEST_CHECK(purchase.wait_for(0s) != std::future_status::ready);
  TEST_CHECK(WaitFor(messenger.Invoke("requestReview")).error_code ==
             "purchase_in_progress");
  TEST_CHECK(WaitFor(std::move(purchase)).kind == ResponseKind::kSuccess);
  TEST_CHECK(WaitFor(messenger.Invoke("requestReview")).kind ==
             ResponseKind::kSuccess);
  return true;
}

// Fake operations resume on raw thread-pool callbacks, unlike the real Store's
// COM-initialized producer. Keep an MTA alive so those callbacks can marshal
// their completion back to the main STA even when this test runs in isolation.
struct TestMtaUsage {
  CO_MTA_USAGE_COOKIE cookie{};
  TestMtaUsage() { check_hresult(CoIncrementMTAUsage(&cookie)); }
  ~TestMtaUsage() { CoDecrementMTAUsage(cookie); }
};

}  // namespace

int main(int argc, char** argv) {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  bool sta = false;
  std::string filter;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--sta") sta = true;
    if (argument.rfind("--test=", 0) == 0) filter = argument.substr(7);
  }
  winrt::init_apartment(sta ? winrt::apartment_type::single_threaded
                            : winrt::apartment_type::multi_threaded);
  const TestMtaUsage mta_usage;

  const std::pair<const char*, bool (*)()> tests[] = {
      {"ReviewDoesNotChangeCatalogOrSubscriptionQueries",
       ReviewDoesNotChangeCatalogOrSubscriptionQueries},
      {"QueryTimeoutDoesNotReleasePendingReview",
       QueryTimeoutDoesNotReleasePendingReview},
      {"InvalidPurchaseAttemptDoesNotReleasePendingReview",
       InvalidPurchaseAttemptDoesNotReleasePendingReview},
      {"AsyncReviewFailuresAllowSubsequentPurchase",
       AsyncReviewFailuresAllowSubsequentPurchase},
      {"SynchronousReviewCompletionAllowsImmediateRetry",
       SynchronousReviewCompletionAllowsImmediateRetry},
      {"ReviewReplyCanImmediatelyStartPurchaseWithoutClearingNewGuard",
       ReviewReplyCanImmediatelyStartPurchaseWithoutClearingNewGuard},
      {"ReviewResultsPreserveStatusesAndJsonSemantics",
       ReviewResultsPreserveStatusesAndJsonSemantics},
      {"ReviewAndPurchaseDialogsAreMutuallyExclusiveWithoutQueryTimeout",
       ReviewAndPurchaseDialogsAreMutuallyExclusiveWithoutQueryTimeout},
      {"ReviewShutdownCancelsAndDiscardsLateReplies",
       ReviewShutdownCancelsAndDiscardsLateReplies},
      {"CanceledNativeReviewAllowsRetry", CanceledNativeReviewAllowsRetry},
      {"ReviewFailuresReleaseDialogGuard", ReviewFailuresReleaseDialogGuard},
      {"ReviewRejectsInvalidWindowAndWrongThreadBeforeStartingStore",
       ReviewRejectsInvalidWindowAndWrongThreadBeforeStartingStore},
      {"NoViewReturnsAnErrorWithoutCreatingStore",
       NoViewReturnsAnErrorWithoutCreatingStore},
      {"PluginDestructionDoesNotTouchMessenger",
       PluginDestructionDoesNotTouchMessenger},
      {"ShutdownCompletesPendingStoreRequests",
       ShutdownCompletesPendingStoreRequests},
      {"StaleMethodHandlerDoesNotAccessDestroyedPlugin",
       StaleMethodHandlerDoesNotAccessDestroyedPlugin},
      {"EveryQueryTimesOutAndCancelsWithoutDuplicateReplies",
       EveryQueryTimesOutAndCancelsWithoutDuplicateReplies},
      {"DelayedPurchaseKeepsItsGuardBeyondQueryDeadline",
       DelayedPurchaseKeepsItsGuardBeyondQueryDeadline},
      {"TimeoutCompletionRaceRepliesExactlyOnce",
       TimeoutCompletionRaceRepliesExactlyOnce},
      {"StoreInitializationFailurePreservesReplyAndPurchaseGuard",
       StoreInitializationFailurePreservesReplyAndPurchaseGuard},
      {"NullAsyncOperationsReturnErrorsInsteadOfDereferencingNull",
       NullAsyncOperationsReturnErrorsInsteadOfDereferencingNull},
      {"SynchronousStoreErrorsReplyAndAllowPurchaseRetry",
       SynchronousStoreErrorsReplyAndAllowPurchaseRetry},
      {"AllNativePurchaseStatusesRoundTrip",
       AllNativePurchaseStatusesRoundTrip},
      {"InactiveExpiredAndUnrelatedLicensesDoNotGrantAccess",
       InactiveExpiredAndUnrelatedLicensesDoNotGrantAccess},
      {"WrongThreadDoesNotCreateStoreOrUseView",
       WrongThreadDoesNotCreateStoreOrUseView},
      {"TimedOutQueryDoesNotPoisonTheNextRequest",
       TimedOutQueryDoesNotPoisonTheNextRequest},
      {"CatalogSerializesNestedPricesAndSubscriptionInfo",
       CatalogSerializesNestedPricesAndSubscriptionInfo},
      {"MissingCatalogPricesReturnErrors", MissingCatalogPricesReturnErrors},
      {"MissingCatalogCollectionsReturnErrors",
       MissingCatalogCollectionsReturnErrors},
      {"EmptyCatalogCollectionsRemainValid",
       EmptyCatalogCollectionsRemainValid},
      {"CatalogQueryErrorsPreserveHResult", CatalogQueryErrorsPreserveHResult},
      {"PluginInstancesUseTheirOwnWindows", PluginInstancesUseTheirOwnWindows},
      {"CheckPurchaseOwnsStoreIdAcrossSuspension",
       CheckPurchaseOwnsStoreIdAcrossSuspension},
      {"PurchaseStatusAndExtendedErrorAreBothAvailable",
       PurchaseStatusAndExtendedErrorAreBothAvailable},
      {"ConcurrentPurchasesAreRejected", ConcurrentPurchasesAreRejected},
  };

  bool selected = false;
  for (const auto& test : tests) {
    if (!filter.empty() && filter != test.first) continue;
    selected = true;
    if (!test.second()) {
      return 1;
    }
    std::cout << "[PASS] " << test.first << std::endl;
  }
  return selected ? 0 : 2;
}
