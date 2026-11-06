#include "windows_iap_plugin.h"

#include <flutter/standard_method_codec.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Services.Store.h>

#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "store_request.h"

using namespace winrt;
using namespace Windows::Foundation::Collections;
using namespace Windows::Services::Store;
namespace foundation = Windows::Foundation;

namespace windows_iap {
namespace {

// Windows Runtime DateTime uses 100-nanosecond ticks since 1601-01-01 UTC.
constexpr int64_t kWinRtTicksPerMillisecond = 10000;
constexpr int64_t kUnixEpochInWinRtTicks = 116444736000000000LL;

enum class PurchaseResponseKind {
  kStatusOnly,
  kDetailed,
};

class PendingStoreUiReset final {
 public:
  explicit PendingStoreUiReset(
      std::shared_ptr<std::atomic<StoreUiOperation>> pending)
      : pending_(std::move(pending)) {}

  ~PendingStoreUiReset() { Release(); }

  void Release() {
    if (pending_) {
      pending_->store(StoreUiOperation::kNone);
      pending_.reset();
    }
  }

  PendingStoreUiReset(const PendingStoreUiReset&) = delete;
  PendingStoreUiReset& operator=(const PendingStoreUiReset&) = delete;

 private:
  std::shared_ptr<std::atomic<StoreUiOperation>> pending_;
};

flutter::EncodableValue Key(const char* value) {
  return flutter::EncodableValue(std::string(value));
}

int64_t DateTimeTicks(const foundation::DateTime& value) {
  return value.time_since_epoch().count();
}

int64_t DateTimeUnixMilliseconds(const foundation::DateTime& value) {
  return (DateTimeTicks(value) - kUnixEpochInWinRtTicks) /
         kWinRtTicksPerMillisecond;
}

bool IsNotExpired(const foundation::DateTime& expiration_date) {
  return expiration_date > winrt::clock::now();
}

int64_t TimeSpanMilliseconds(const foundation::TimeSpan& value) {
  return value.count() / kWinRtTicksPerMillisecond;
}

std::string ProductStoreIdFromSkuStoreId(const std::string& sku_store_id) {
  const auto separator = sku_store_id.find('/');
  return separator == std::string::npos ? sku_store_id
                                        : sku_store_id.substr(0, separator);
}

std::string HResultCode(hresult code) {
  char buffer[16] = {};
  snprintf(buffer, sizeof(buffer), "0x%08X",
           static_cast<unsigned int>(code.value));
  return std::string(buffer);
}

std::string HResultCode(const hresult_error& error) {
  return HResultCode(error.code());
}

std::string HResultMessage(const hresult_error& error) {
  const auto message = to_string(error.message());
  return message.empty() ? "Microsoft Store operation failed." : message;
}

void SendHResultError(
    const hresult_error& error,
    flutter::MethodResult<flutter::EncodableValue>* result) {
  result->Error(HResultCode(error), HResultMessage(error));
}

std::optional<std::string> StringArgument(
    const flutter::MethodCall<flutter::EncodableValue>& call,
    const char* name) {
  if (call.arguments() == nullptr) {
    return std::nullopt;
  }
  const auto* arguments =
      std::get_if<flutter::EncodableMap>(call.arguments());
  if (arguments == nullptr) {
    return std::nullopt;
  }
  const auto iterator = arguments->find(Key(name));
  if (iterator == arguments->end()) {
    return std::nullopt;
  }
  const auto* value = std::get_if<std::string>(&iterator->second);
  return value == nullptr ? std::nullopt
                          : std::optional<std::string>(*value);
}

flutter::EncodableMap PriceToMap(const StorePrice& price) {
  if (price == nullptr) {
    throw hresult_error(E_UNEXPECTED, L"Microsoft Store returned no price.");
  }
  flutter::EncodableMap value;
  value[Key("currencyCode")] = to_string(price.CurrencyCode());
  value[Key("formattedBasePrice")] = to_string(price.FormattedBasePrice());
  value[Key("formattedPrice")] = to_string(price.FormattedPrice());
  value[Key("formattedRecurrencePrice")] =
      to_string(price.FormattedRecurrencePrice());
  value[Key("unformattedBasePrice")] =
      to_string(price.UnformattedBasePrice());
  value[Key("unformattedPrice")] = to_string(price.UnformattedPrice());
  value[Key("unformattedRecurrencePrice")] =
      to_string(price.UnformattedRecurrencePrice());
  value[Key("isOnSale")] = price.IsOnSale();
  value[Key("saleEndDateEpochMilliseconds")] =
      DateTimeUnixMilliseconds(price.SaleEndDate());
  return value;
}

flutter::EncodableMap SubscriptionInfoToMap(
    const StoreSubscriptionInfo& subscription_info) {
  flutter::EncodableMap value;
  value[Key("billingPeriod")] =
      static_cast<int64_t>(subscription_info.BillingPeriod());
  value[Key("billingPeriodUnit")] = static_cast<int32_t>(
      subscription_info.BillingPeriodUnit());
  value[Key("hasTrialPeriod")] = subscription_info.HasTrialPeriod();
  value[Key("trialPeriod")] =
      static_cast<int64_t>(subscription_info.TrialPeriod());
  value[Key("trialPeriodUnit")] =
      static_cast<int32_t>(subscription_info.TrialPeriodUnit());
  return value;
}

flutter::EncodableMap CollectionDataToMap(
    const StoreCollectionData& collection_data) {
  flutter::EncodableMap value;
  value[Key("acquiredDateEpochMilliseconds")] =
      DateTimeUnixMilliseconds(collection_data.AcquiredDate());
  value[Key("startDateEpochMilliseconds")] =
      DateTimeUnixMilliseconds(collection_data.StartDate());
  value[Key("endDateEpochMilliseconds")] =
      DateTimeUnixMilliseconds(collection_data.EndDate());
  value[Key("isTrial")] = collection_data.IsTrial();
  value[Key("trialTimeRemainingMilliseconds")] =
      TimeSpanMilliseconds(collection_data.TrialTimeRemaining());
  value[Key("campaignId")] = to_string(collection_data.CampaignId());
  value[Key("developerOfferId")] =
      to_string(collection_data.DeveloperOfferId());
  value[Key("extendedJsonData")] =
      to_string(collection_data.ExtendedJsonData());
  return value;
}

flutter::EncodableMap AvailabilityToMap(
    const StoreAvailability& availability) {
  flutter::EncodableMap value;
  value[Key("storeId")] = to_string(availability.StoreId());
  value[Key("endDateEpochMilliseconds")] =
      DateTimeUnixMilliseconds(availability.EndDate());
  value[Key("price")] = PriceToMap(availability.Price());
  value[Key("extendedJsonData")] =
      to_string(availability.ExtendedJsonData());
  return value;
}

flutter::EncodableMap SkuToMap(const StoreSku& sku) {
  flutter::EncodableMap value;
  value[Key("storeId")] = to_string(sku.StoreId());
  value[Key("title")] = to_string(sku.Title());
  value[Key("description")] = to_string(sku.Description());
  value[Key("language")] = to_string(sku.Language());
  value[Key("isInUserCollection")] = sku.IsInUserCollection();
  value[Key("isSubscription")] = sku.IsSubscription();
  value[Key("isTrial")] = sku.IsTrial();
  value[Key("customDeveloperData")] =
      to_string(sku.CustomDeveloperData());
  value[Key("price")] = PriceToMap(sku.Price());
  value[Key("extendedJsonData")] = to_string(sku.ExtendedJsonData());

  const auto subscription_info = sku.SubscriptionInfo();
  if (sku.IsSubscription() && subscription_info != nullptr) {
    value[Key("subscriptionInfo")] =
        SubscriptionInfoToMap(subscription_info);
  }
  const auto collection_data = sku.CollectionData();
  if (collection_data != nullptr) {
    value[Key("collectionData")] =
        CollectionDataToMap(collection_data);
  }

  flutter::EncodableList availabilities;
  const auto store_availabilities = sku.Availabilities();
  if (store_availabilities == nullptr) {
    throw hresult_error(E_UNEXPECTED,
                        L"Microsoft Store returned no availability collection.");
  }
  for (const StoreAvailability& availability : store_availabilities) {
    if (availability != nullptr) {
      availabilities.emplace_back(AvailabilityToMap(availability));
    }
  }
  value[Key("availabilities")] = availabilities;
  return value;
}

flutter::EncodableMap ProductToMap(const StoreProduct& product) {
  flutter::EncodableMap value;
  const auto price = product.Price();
  value[Key("title")] = to_string(product.Title());
  value[Key("description")] = to_string(product.Description());
  value[Key("price")] =
      price == nullptr ? std::string() : to_string(price.FormattedPrice());
  value[Key("inCollection")] = product.IsInUserCollection();
  value[Key("productKind")] = to_string(product.ProductKind());
  value[Key("storeId")] = to_string(product.StoreId());
  value[Key("inAppOfferToken")] = to_string(product.InAppOfferToken());
  value[Key("language")] = to_string(product.Language());
  if (price != nullptr) {
    value[Key("storePrice")] = PriceToMap(price);
  }
  value[Key("extendedJsonData")] =
      to_string(product.ExtendedJsonData());

  flutter::EncodableList skus;
  const auto store_skus = product.Skus();
  if (store_skus == nullptr) {
    throw hresult_error(E_UNEXPECTED,
                        L"Microsoft Store returned no SKU collection.");
  }
  for (const StoreSku& sku : store_skus) {
    if (sku != nullptr) {
      skus.emplace_back(SkuToMap(sku));
    }
  }
  value[Key("skus")] = skus;
  return value;
}

flutter::EncodableMap LicenseToMap(const StoreLicense& license) {
  const auto expiration_date = license.ExpirationDate();
  const auto sku_store_id = to_string(license.SkuStoreId());
  flutter::EncodableMap value;

  // Kept for backwards compatibility only. Microsoft documents that
  // StoreLicense.IsActive currently always returns true.
  value[Key("isActive")] = license.IsActive();
  value[Key("isEntitled")] = IsNotExpired(expiration_date);
  value[Key("skuStoreId")] = sku_store_id;
  value[Key("productStoreId")] =
      ProductStoreIdFromSkuStoreId(sku_store_id);
  value[Key("inAppOfferToken")] = to_string(license.InAppOfferToken());
  value[Key("expirationDate")] = DateTimeTicks(expiration_date);
  value[Key("expirationDateEpochMilliseconds")] =
      DateTimeUnixMilliseconds(expiration_date);
  value[Key("extendedJsonData")] =
      to_string(license.ExtendedJsonData());
  return value;
}

flutter::EncodableMap AddOnLicensesToMap(
    const StoreAppLicense& app_license) {
  flutter::EncodableMap licenses;
  const auto add_on_licenses = app_license.AddOnLicenses();
  if (add_on_licenses == nullptr) {
    return licenses;
  }

  for (const IKeyValuePair<hstring, StoreLicense>& entry : add_on_licenses) {
    if (entry.Value() != nullptr) {
      licenses[flutter::EncodableValue(to_string(entry.Key()))] =
          LicenseToMap(entry.Value());
    }
  }
  return licenses;
}

flutter::EncodableMap AppLicenseToMap(const StoreAppLicense& app_license) {
  const auto expiration_date = app_license.ExpirationDate();
  flutter::EncodableMap value;
  value[Key("isActive")] = app_license.IsActive();
  value[Key("isTrial")] = app_license.IsTrial();
  value[Key("skuStoreId")] = to_string(app_license.SkuStoreId());
  value[Key("expirationDateEpochMilliseconds")] =
      DateTimeUnixMilliseconds(expiration_date);
  value[Key("trialTimeRemainingMilliseconds")] =
      TimeSpanMilliseconds(app_license.TrialTimeRemaining());
  value[Key("trialUniqueId")] = to_string(app_license.TrialUniqueId());
  value[Key("extendedJsonData")] =
      to_string(app_license.ExtendedJsonData());
  value[Key("addOnLicenses")] = AddOnLicensesToMap(app_license);
  return value;
}

bool LicenseMatchesStoreId(const std::string& collection_key,
                           const StoreLicense& license,
                           const std::string& store_id) {
  if (collection_key == store_id) {
    return true;
  }
  const auto sku_store_id = to_string(license.SkuStoreId());
  if (sku_store_id == store_id) {
    return true;
  }
  const auto sku_prefix = store_id + "/";
  if (sku_store_id.rfind(sku_prefix, 0) == 0) {
    return true;
  }
  return to_string(license.InAppOfferToken()) == store_id;
}

foundation::IAsyncAction MakePurchase(
    std::shared_ptr<StoreApi> store, hstring store_id,
    PurchaseResponseKind response_kind,
    std::shared_ptr<std::atomic<StoreUiOperation>> ui_pending,
    std::shared_ptr<StoreRequest> result) {
  PendingStoreUiReset reset(std::move(ui_pending));
  try {
    auto operation = store->RequestPurchaseAsync(std::move(store_id));
    result->Watch(operation, std::chrono::milliseconds::zero());
    const StorePurchaseResult purchase = co_await operation;
    if (!result->pending()) co_return;
    if (purchase == nullptr) {
      throw hresult_error(E_UNEXPECTED,
                          L"Microsoft Store returned no purchase result.");
    }

    const auto status = static_cast<int32_t>(purchase.Status());
    if (response_kind == PurchaseResponseKind::kStatusOnly) {
      reset.Release();
      result->Success(flutter::EncodableValue(status));
      co_return;
    }

    const auto extended_error = purchase.ExtendedError();
    flutter::EncodableMap details;
    details[Key("status")] = status;
    details[Key("extendedErrorCode")] =
        extended_error.value == S_OK
            ? flutter::EncodableValue()
            : flutter::EncodableValue(HResultCode(extended_error));
    reset.Release();
    result->Success(flutter::EncodableValue(details));
  } catch (const hresult_error& error) {
    reset.Release();
    SendHResultError(error, result.get());
  } catch (const std::exception& error) {
    reset.Release();
    result->Error("native_error", error.what());
  }
}

flutter::EncodableValue UpdatedExistingReview(StoreRateAndReviewStatus status,
                                              const hstring& json) {
  using namespace Windows::Data::Json;
  if (status != StoreRateAndReviewStatus::Succeeded || json.empty()) return {};
  JsonObject root;
  if (!JsonObject::TryParse(json, root)) return {};
  const auto response_status = root.TryLookup(L"status");
  if (!response_status ||
      response_status.ValueType() != JsonValueType::String ||
      response_status.GetString() != L"success") {
    return {};
  }
  const auto data = root.TryLookup(L"data");
  if (!data || data.ValueType() != JsonValueType::Object) return {};
  const auto updated = data.GetObject().TryLookup(L"updated");
  if (!updated || updated.ValueType() != JsonValueType::Boolean) return {};
  return flutter::EncodableValue(updated.GetBoolean());
}

foundation::IAsyncAction RequestReview(
    std::shared_ptr<StoreApi> store,
    std::shared_ptr<std::atomic<StoreUiOperation>> ui_pending,
    std::shared_ptr<StoreRequest> result) {
  PendingStoreUiReset reset(std::move(ui_pending));
  try {
    auto operation = store->RequestRateAndReviewAppAsync();
    result->Watch(operation, std::chrono::milliseconds::zero());
    const StoreRateAndReviewResult review = co_await operation;
    if (!result->pending()) co_return;
    if (!review) {
      throw hresult_error(E_UNEXPECTED,
                          L"Microsoft Store returned no review result.");
    }
    const auto status = review.Status();
    const auto extended_error = review.ExtendedError();
    const auto json = review.ExtendedJsonData();
    flutter::EncodableMap details;
    details[Key("status")] = static_cast<int32_t>(status);
    details[Key("extendedErrorCode")] =
        extended_error.value == S_OK
            ? flutter::EncodableValue()
            : flutter::EncodableValue(HResultCode(extended_error));
    details[Key("extendedJsonData")] = to_string(json);
    // WasUpdated is the official property, not the JSON data.updated flag.
    details[Key("wasUpdated")] = review.WasUpdated();
    details[Key("updatedExistingReview")] = UpdatedExistingReview(status, json);
    reset.Release();
    result->Success(flutter::EncodableValue(details));
  } catch (const hresult_error& error) {
    reset.Release();
    SendHResultError(error, result.get());
  } catch (const std::exception& error) {
    reset.Release();
    result->Error("native_error", error.what());
  } catch (...) {
    reset.Release();
    result->Error("native_error", "Unexpected Microsoft Store review failure.");
  }
}

foundation::IAsyncAction GetProducts(std::shared_ptr<StoreApi> store,
                                     std::chrono::milliseconds timeout,
                                     std::shared_ptr<StoreRequest> result) {
  try {
    auto operation = store->GetProductsAsync();
    result->Watch(operation, timeout);
    const StoreProductQueryResult query = co_await operation;
    if (!result->pending()) co_return;
    if (query == nullptr) {
      throw hresult_error(E_UNEXPECTED,
                          L"Microsoft Store returned no product result.");
    }
    if (query.ExtendedError().value != S_OK) {
      result->Error(HResultCode(query.ExtendedError()),
                    "Unable to load Microsoft Store products.");
      co_return;
    }

    flutter::EncodableList products;
    const auto query_products = query.Products();
    if (query_products != nullptr) {
      for (const IKeyValuePair<hstring, StoreProduct>& entry : query_products) {
        if (entry.Value() != nullptr) {
          products.emplace_back(ProductToMap(entry.Value()));
        }
      }
    }
    result->Success(flutter::EncodableValue(products));
  } catch (const hresult_error& error) {
    SendHResultError(error, result.get());
  } catch (const std::exception& error) {
    result->Error("native_error", error.what());
  }
}

foundation::IAsyncAction GetAppLicense(std::shared_ptr<StoreApi> store,
                                       std::chrono::milliseconds timeout,
                                       std::shared_ptr<StoreRequest> result) {
  try {
    auto operation = store->GetAppLicenseAsync();
    result->Watch(operation, timeout);
    const StoreAppLicense app_license = co_await operation;
    if (!result->pending()) co_return;
    if (app_license == nullptr) {
      throw hresult_error(E_UNEXPECTED,
                          L"Microsoft Store returned no app license.");
    }
    result->Success(flutter::EncodableValue(AppLicenseToMap(app_license)));
  } catch (const hresult_error& error) {
    SendHResultError(error, result.get());
  } catch (const std::exception& error) {
    result->Error("native_error", error.what());
  }
}

foundation::IAsyncAction GetAddonLicenses(
    std::shared_ptr<StoreApi> store, std::chrono::milliseconds timeout,
    std::shared_ptr<StoreRequest> result) {
  try {
    auto operation = store->GetAppLicenseAsync();
    result->Watch(operation, timeout);
    const StoreAppLicense app_license = co_await operation;
    if (!result->pending()) co_return;
    if (app_license == nullptr) {
      throw hresult_error(E_UNEXPECTED,
                          L"Microsoft Store returned no app license.");
    }
    result->Success(flutter::EncodableValue(AddOnLicensesToMap(app_license)));
  } catch (const hresult_error& error) {
    SendHResultError(error, result.get());
  } catch (const std::exception& error) {
    result->Error("native_error", error.what());
  }
}

foundation::IAsyncAction CheckPurchase(std::shared_ptr<StoreApi> store,
                                       std::string store_id,
                                       std::chrono::milliseconds timeout,
                                       std::shared_ptr<StoreRequest> result) {
  try {
    auto operation = store->GetAppLicenseAsync();
    result->Watch(operation, timeout);
    const StoreAppLicense app_license = co_await operation;
    if (!result->pending()) co_return;
    if (app_license == nullptr || !app_license.IsActive()) {
      result->Success(flutter::EncodableValue(false));
      co_return;
    }

    const auto add_on_licenses = app_license.AddOnLicenses();
    if (add_on_licenses != nullptr) {
      for (const IKeyValuePair<hstring, StoreLicense>& entry :
           add_on_licenses) {
        const StoreLicense license = entry.Value();
        if (license == nullptr) {
          continue;
        }
        const bool matches =
            store_id.empty() ||
            LicenseMatchesStoreId(to_string(entry.Key()), license, store_id);
        if (matches && IsNotExpired(license.ExpirationDate())) {
          result->Success(flutter::EncodableValue(true));
          co_return;
        }
      }
    }
    result->Success(flutter::EncodableValue(false));
  } catch (const hresult_error& error) {
    SendHResultError(error, result.get());
  } catch (const std::exception& error) {
    result->Error("native_error", error.what());
  }
}

}  // namespace

// static
void WindowsIapPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows* registrar) {
  if (registrar == nullptr) {
    return;
  }

  auto view_window = [registrar]() -> HWND {
    auto* view = registrar->GetView();
    return view == nullptr ? nullptr : view->GetNativeWindow();
  };
  auto plugin = std::make_unique<WindowsIapPlugin>(registrar->messenger(),
                                                   std::move(view_window));
  registrar->AddPlugin(std::move(plugin));
}

WindowsIapPlugin::WindowsIapPlugin(flutter::BinaryMessenger* messenger,
                                   std::function<HWND()> view_window,
                                   StoreApiFactory store_factory,
                                   std::chrono::milliseconds query_timeout)
    : channel_(
          std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
              messenger, "windows_store_iap",
              &flutter::StandardMethodCodec::GetInstance())),
      view_window_(std::move(view_window)),
      store_factory_(std::move(store_factory)),
      platform_thread_(::GetCurrentThreadId()),
      query_timeout_(query_timeout),
      alive_(std::make_shared<std::atomic_bool>(true)),
      requests_(std::make_unique<StoreRequestManager>()),
      ui_pending_(std::make_shared<std::atomic<StoreUiOperation>>(
          StoreUiOperation::kNone)) {
  channel_->SetMethodCallHandler(
      [this, alive = alive_](const auto& call, auto result) {
        if (!alive->load()) {
          result->Error("plugin_disposed",
                        "Microsoft Store plugin is shutting down.");
          return;
        }
        HandleMethodCall(call, std::move(result));
      });
}

// The registrar destroys owned plugins while it is tearing down the engine.
// MethodChannel destruction does not unregister its handler, and the registrar
// destroys the BinaryMessenger immediately after the plugins. Calling
// SetMethodCallHandler here can therefore re-enter a messenger whose native
// registrar is already being destroyed.
WindowsIapPlugin::~WindowsIapPlugin() {
  alive_->store(false);
  requests_->Shutdown();
}

std::shared_ptr<StoreApi> WindowsIapPlugin::CreateStoreForRequest() const {
  if (::GetCurrentThreadId() != platform_thread_) {
    throw hresult_error(
        RPC_E_WRONG_THREAD,
        L"Microsoft Store calls must start on the Flutter platform thread.");
  }
  if (!view_window_) {
    throw hresult_error(
        HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE),
        L"The Flutter view is unavailable.");
  }

  const HWND root_window = ResolveStoreWindow(view_window_());
  if (root_window == nullptr) {
    throw hresult_error(
        HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE),
        L"The Flutter view does not have a valid native window.");
  }
  if (!store_factory_) {
    throw hresult_error(E_UNEXPECTED,
                        L"The Microsoft Store client is unavailable.");
  }

  auto store = store_factory_(root_window);
  if (!store) {
    throw hresult_error(E_UNEXPECTED,
                        L"The Microsoft Store client is unavailable.");
  }
  return store;
}

void WindowsIapPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue>& method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  auto request = requests_->Add(std::move(result));
  const auto& method = method_call.method_name();
  if (method != "makePurchase" && method != "makePurchaseDetailed" &&
      method != "getProducts" && method != "checkPurchase" &&
      method != "getAddonLicenses" && method != "getAppLicense" &&
      method != "requestReview") {
    request->NotImplemented();
    return;
  }

  if (method == "makePurchase" || method == "makePurchaseDetailed" ||
      method == "requestReview") {
    const bool review = method == "requestReview";
    const auto store_id = StringArgument(method_call, "storeId");
    if (!review && (!store_id.has_value() || store_id->empty())) {
      request->Error("invalid_arguments", "storeId must not be empty.");
      return;
    }
    auto pending = StoreUiOperation::kNone;
    if (!ui_pending_->compare_exchange_strong(
            pending,
            review ? StoreUiOperation::kReview : StoreUiOperation::kPurchase)) {
      request->Error(pending == StoreUiOperation::kReview
                         ? "review_in_progress"
                         : "purchase_in_progress",
                     "A Microsoft Store purchase or review dialog is already "
                     "in progress.");
      return;
    }

    try {
      auto store = CreateStoreForRequest();
      if (review) {
        RequestReview(std::move(store), ui_pending_, request);
      } else {
        const auto native_store_id = to_hstring(*store_id);
        MakePurchase(std::move(store), native_store_id,
                     method == "makePurchaseDetailed"
                         ? PurchaseResponseKind::kDetailed
                         : PurchaseResponseKind::kStatusOnly,
                     ui_pending_, request);
      }
    } catch (const hresult_error& error) {
      ui_pending_->store(StoreUiOperation::kNone);
      SendHResultError(error, request.get());
    } catch (const std::exception& error) {
      ui_pending_->store(StoreUiOperation::kNone);
      request->Error("native_error", error.what());
    } catch (...) {
      ui_pending_->store(StoreUiOperation::kNone);
      request->Error("native_error", "Unexpected Microsoft Store UI failure.");
    }
    return;
  }

  std::shared_ptr<StoreApi> store;
  try {
    store = CreateStoreForRequest();
  } catch (const hresult_error& error) {
    SendHResultError(error, request.get());
    return;
  } catch (const std::exception& error) {
    request->Error("native_error", error.what());
    return;
  }

  if (method == "getProducts") {
    GetProducts(std::move(store), query_timeout_, request);
  } else if (method == "checkPurchase") {
    const auto store_id = StringArgument(method_call, "storeId");
    CheckPurchase(std::move(store), store_id.value_or(""), query_timeout_,
                  request);
  } else if (method == "getAddonLicenses") {
    GetAddonLicenses(std::move(store), query_timeout_, request);
  } else if (method == "getAppLicense") {
    GetAppLicense(std::move(store), query_timeout_, request);
  }
}

}  // namespace windows_iap
