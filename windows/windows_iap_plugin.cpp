#include "windows_iap_plugin.h"

// This must be included before many other Windows headers.
#include <windows.h>

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>

#include <cstdio>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>

#include <shobjidl.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Services.Store.h>

using namespace winrt;
using namespace Windows::Foundation::Collections;
using namespace Windows::Services::Store;
namespace foundation = Windows::Foundation;

namespace windows_iap {
namespace {

// Windows Runtime DateTime uses 100-nanosecond ticks since 1601-01-01 UTC.
constexpr int64_t kWinRtTicksPerMillisecond = 10000;
constexpr int64_t kUnixEpochInWinRtTicks = 116444736000000000LL;

HWND g_root_window = nullptr;

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

StoreContext GetStore() {
  StoreContext store = StoreContext::GetDefault();
  if (g_root_window != nullptr) {
    auto initialize_with_window = store.try_as<IInitializeWithWindow>();
    if (initialize_with_window != nullptr) {
      check_hresult(initialize_with_window->Initialize(g_root_window));
    }
  }
  return store;
}

std::string HResultCode(const hresult_error& error) {
  char buffer[16] = {};
  snprintf(buffer, sizeof(buffer), "0x%08X",
           static_cast<unsigned int>(error.code().value));
  return std::string(buffer);
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

  if (sku.IsSubscription() && sku.SubscriptionInfo() != nullptr) {
    value[Key("subscriptionInfo")] =
        SubscriptionInfoToMap(sku.SubscriptionInfo());
  }
  if (sku.CollectionData() != nullptr) {
    value[Key("collectionData")] =
        CollectionDataToMap(sku.CollectionData());
  }

  flutter::EncodableList availabilities;
  for (const StoreAvailability& availability : sku.Availabilities()) {
    availabilities.emplace_back(AvailabilityToMap(availability));
  }
  value[Key("availabilities")] = availabilities;
  return value;
}

flutter::EncodableMap ProductToMap(const StoreProduct& product) {
  flutter::EncodableMap value;
  value[Key("title")] = to_string(product.Title());
  value[Key("description")] = to_string(product.Description());
  value[Key("price")] = to_string(product.Price().FormattedPrice());
  value[Key("inCollection")] = product.IsInUserCollection();
  value[Key("productKind")] = to_string(product.ProductKind());
  value[Key("storeId")] = to_string(product.StoreId());
  value[Key("inAppOfferToken")] = to_string(product.InAppOfferToken());
  value[Key("language")] = to_string(product.Language());
  value[Key("storePrice")] = PriceToMap(product.Price());
  value[Key("extendedJsonData")] =
      to_string(product.ExtendedJsonData());

  flutter::EncodableList skus;
  for (const StoreSku& sku : product.Skus()) {
    skus.emplace_back(SkuToMap(sku));
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
  for (const IKeyValuePair<hstring, StoreLicense>& entry :
       app_license.AddOnLicenses()) {
    licenses[flutter::EncodableValue(to_string(entry.Key()))] =
        LicenseToMap(entry.Value());
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
    const hstring& store_id,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  try {
    const StorePurchaseResult purchase =
        co_await GetStore().RequestPurchaseAsync(store_id);
    if (purchase.ExtendedError().value != S_OK) {
      result->Error(std::to_string(purchase.ExtendedError().value),
                    "Microsoft Store rejected the purchase request.");
      co_return;
    }

    result->Success(flutter::EncodableValue(
        static_cast<int32_t>(purchase.Status())));
  } catch (const hresult_error& error) {
    SendHResultError(error, result.get());
  } catch (const std::exception& error) {
    result->Error("native_error", error.what());
  }
}

foundation::IAsyncAction GetProducts(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  try {
    const StoreProductQueryResult query =
        co_await GetStore().GetAssociatedStoreProductsAsync(
            {L"Consumable", L"Durable", L"UnmanagedConsumable"});
    if (query.ExtendedError().value != S_OK) {
      result->Error(std::to_string(query.ExtendedError().value),
                    "Unable to load Microsoft Store products.");
      co_return;
    }

    flutter::EncodableList products;
    for (const IKeyValuePair<hstring, StoreProduct>& entry : query.Products()) {
      products.emplace_back(ProductToMap(entry.Value()));
    }
    result->Success(flutter::EncodableValue(products));
  } catch (const hresult_error& error) {
    SendHResultError(error, result.get());
  } catch (const std::exception& error) {
    result->Error("native_error", error.what());
  }
}

foundation::IAsyncAction GetAppLicense(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  try {
    const StoreAppLicense app_license = co_await GetStore().GetAppLicenseAsync();
    result->Success(flutter::EncodableValue(AppLicenseToMap(app_license)));
  } catch (const hresult_error& error) {
    SendHResultError(error, result.get());
  } catch (const std::exception& error) {
    result->Error("native_error", error.what());
  }
}

foundation::IAsyncAction GetAddonLicenses(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  try {
    const StoreAppLicense app_license = co_await GetStore().GetAppLicenseAsync();
    result->Success(
        flutter::EncodableValue(AddOnLicensesToMap(app_license)));
  } catch (const hresult_error& error) {
    SendHResultError(error, result.get());
  } catch (const std::exception& error) {
    result->Error("native_error", error.what());
  }
}

foundation::IAsyncAction CheckPurchase(
    const std::string& store_id,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  try {
    const StoreAppLicense app_license = co_await GetStore().GetAppLicenseAsync();
    if (!app_license.IsActive()) {
      result->Success(flutter::EncodableValue(false));
      co_return;
    }

    for (const IKeyValuePair<hstring, StoreLicense>& entry :
         app_license.AddOnLicenses()) {
      const StoreLicense license = entry.Value();
      const bool matches =
          store_id.empty() ||
          LicenseMatchesStoreId(to_string(entry.Key()), license, store_id);
      if (matches && IsNotExpired(license.ExpirationDate())) {
        result->Success(flutter::EncodableValue(true));
        co_return;
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
  g_root_window =
      ::GetAncestor(registrar->GetView()->GetNativeWindow(), GA_ROOT);

  auto channel =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          registrar->messenger(), "windows_store_iap",
          &flutter::StandardMethodCodec::GetInstance());

  auto plugin = std::make_unique<WindowsIapPlugin>();
  channel->SetMethodCallHandler(
      [plugin_pointer = plugin.get()](const auto& call, auto result) {
        plugin_pointer->HandleMethodCall(call, std::move(result));
      });
  registrar->AddPlugin(std::move(plugin));
}

WindowsIapPlugin::WindowsIapPlugin() = default;
WindowsIapPlugin::~WindowsIapPlugin() = default;

void WindowsIapPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue>& method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  const auto& method = method_call.method_name();
  if (method == "makePurchase") {
    const auto store_id = StringArgument(method_call, "storeId");
    if (!store_id.has_value() || store_id->empty()) {
      result->Error("invalid_arguments", "storeId must not be empty.");
      return;
    }
    MakePurchase(to_hstring(*store_id), std::move(result));
  } else if (method == "getProducts") {
    GetProducts(std::move(result));
  } else if (method == "checkPurchase") {
    const auto store_id = StringArgument(method_call, "storeId");
    CheckPurchase(store_id.value_or(""), std::move(result));
  } else if (method == "getAddonLicenses") {
    GetAddonLicenses(std::move(result));
  } else if (method == "getAppLicense") {
    GetAppLicense(std::move(result));
  } else {
    result->NotImplemented();
  }
}

}  // namespace windows_iap
