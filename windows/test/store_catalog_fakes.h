#ifndef WINDOWS_STORE_IAP_TEST_STORE_CATALOG_FAKES_H_
#define WINDOWS_STORE_IAP_TEST_STORE_CATALOG_FAKES_H_

#include <winrt/Windows.Foundation.Collections.h>

#include <chrono>
#include <utility>

#include "store_api.h"

namespace windows_iap::testing {
using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Collections;
using namespace winrt::Windows::Services::Store;
using namespace std::chrono_literals;

template <typename Runtime, typename Implementation, typename... Args>
Runtime MakeStoreObject(Args&&... args) {
  auto instance = make<Implementation>(std::forward<Args>(args)...);
  Runtime result{nullptr};
  attach_abi(result, detach_abi(instance));
  return result;
}

enum class CatalogShape {
  kComplete,
  kNoSkuPrice,
  kNoAvailabilityPrice,
  kNoSkus,
  kNoAvailabilities,
  kEmptySkus,
  kEmptyAvailabilities,
  kQueryError
};

struct Price : implements<Price, IStorePrice, IStorePrice2> {
  hstring CurrencyCode() const { return L"USD"; }
  hstring FormattedBasePrice() const { return L"$5.00"; }
  hstring FormattedPrice() const { return L"$3.00"; }
  hstring FormattedRecurrencePrice() const { return L"$3.00/month"; }
  hstring UnformattedBasePrice() const { return L"5.00"; }
  hstring UnformattedPrice() const { return L"3.00"; }
  hstring UnformattedRecurrencePrice() const { return L"3.00"; }
  bool IsOnSale() const { return true; }
  DateTime SaleEndDate() const { return clock::now() + 24h; }
};

struct Subscription : implements<Subscription, IStoreSubscriptionInfo> {
  uint32_t BillingPeriod() const { return 1; }
  StoreDurationUnit BillingPeriodUnit() const {
    return StoreDurationUnit::Month;
  }
  bool HasTrialPeriod() const { return true; }
  uint32_t TrialPeriod() const { return 7; }
  StoreDurationUnit TrialPeriodUnit() const { return StoreDurationUnit::Day; }
};

struct Collection : implements<Collection, IStoreCollectionData> {
  DateTime AcquiredDate() const { return clock::now() - 1h; }
  DateTime StartDate() const { return clock::now() - 1h; }
  DateTime EndDate() const { return clock::now() + 24h; }
  TimeSpan TrialTimeRemaining() const { return 1h; }
  bool IsTrial() const { return true; }
  hstring CampaignId() const { return L"test-campaign"; }
  hstring DeveloperOfferId() const { return L"test-offer"; }
  hstring ExtendedJsonData() const { return L"{}"; }
};

struct Availability : implements<Availability, IStoreAvailability> {
  explicit Availability(CatalogShape shape) : shape_(shape) {}
  hstring StoreId() const { return L"test-product/sku/availability"; }
  DateTime EndDate() const { return clock::now() + 24h; }
  StorePrice Price() const {
    return shape_ == CatalogShape::kNoAvailabilityPrice
               ? StorePrice{nullptr}
               : MakeStoreObject<StorePrice, testing::Price>();
  }
  hstring ExtendedJsonData() const { return L"{}"; }
  IAsyncOperation<StorePurchaseResult> RequestPurchaseAsync() {
    return nullptr;
  }
  IAsyncOperation<StorePurchaseResult> RequestPurchaseAsync(
      const StorePurchaseProperties&) {
    return nullptr;
  }

 private:
  CatalogShape shape_;
};

struct Sku : implements<Sku, IStoreSku> {
  explicit Sku(CatalogShape shape) : shape_(shape) {}
  hstring StoreId() const { return L"test-product/sku"; }
  hstring Title() const { return L"Monthly"; }
  hstring Description() const { return L"Monthly membership"; }
  hstring Language() const { return L"en-US"; }
  bool IsInUserCollection() const { return true; }
  bool IsSubscription() const { return true; }
  bool IsTrial() const { return false; }
  hstring CustomDeveloperData() const { return L"custom"; }
  hstring ExtendedJsonData() const { return L"{}"; }
  StorePrice Price() const {
    return shape_ == CatalogShape::kNoSkuPrice
               ? StorePrice{nullptr}
               : MakeStoreObject<StorePrice, testing::Price>();
  }
  StoreSubscriptionInfo SubscriptionInfo() const {
    return MakeStoreObject<StoreSubscriptionInfo, Subscription>();
  }
  StoreCollectionData CollectionData() const {
    return MakeStoreObject<StoreCollectionData, Collection>();
  }
  IVectorView<StoreAvailability> Availabilities() const {
    if (shape_ == CatalogShape::kNoAvailabilities) return nullptr;
    auto items = single_threaded_vector<StoreAvailability>();
    if (shape_ != CatalogShape::kEmptyAvailabilities) {
      items.Append(nullptr);
      items.Append(MakeStoreObject<StoreAvailability, Availability>(shape_));
    }
    return items.GetView();
  }
  IVectorView<StoreImage> Images() const { return nullptr; }
  IVectorView<StoreVideo> Videos() const { return nullptr; }
  IVectorView<hstring> BundledSkus() const { return nullptr; }
  IAsyncOperation<bool> GetIsInstalledAsync() { co_return false; }
  IAsyncOperation<StorePurchaseResult> RequestPurchaseAsync() {
    return nullptr;
  }
  IAsyncOperation<StorePurchaseResult> RequestPurchaseAsync(
      const StorePurchaseProperties&) {
    return nullptr;
  }

 private:
  CatalogShape shape_;
};

struct Product : implements<Product, IStoreProduct> {
  explicit Product(CatalogShape shape) : shape_(shape) {}
  hstring StoreId() const { return L"test-product"; }
  hstring Title() const { return L"Pro"; }
  hstring Description() const { return L"Membership"; }
  hstring Language() const { return L"en-US"; }
  hstring ProductKind() const { return L"Durable"; }
  hstring InAppOfferToken() const { return L"test-offer"; }
  bool IsInUserCollection() const { return true; }
  bool HasDigitalDownload() const { return false; }
  StorePrice Price() const {
    return MakeStoreObject<StorePrice, testing::Price>();
  }
  hstring ExtendedJsonData() const { return L"{}"; }
  IVectorView<StoreSku> Skus() const {
    if (shape_ == CatalogShape::kNoSkus) return nullptr;
    auto items = single_threaded_vector<StoreSku>();
    if (shape_ != CatalogShape::kEmptySkus) {
      items.Append(nullptr);
      items.Append(MakeStoreObject<StoreSku, Sku>(shape_));
    }
    return items.GetView();
  }
  IVectorView<hstring> Keywords() const { return nullptr; }
  IVectorView<StoreImage> Images() const { return nullptr; }
  IVectorView<StoreVideo> Videos() const { return nullptr; }
  Uri LinkUri() const { return nullptr; }
  IAsyncOperation<bool> GetIsAnySkuInstalledAsync() { co_return false; }
  IAsyncOperation<StorePurchaseResult> RequestPurchaseAsync() {
    return nullptr;
  }
  IAsyncOperation<StorePurchaseResult> RequestPurchaseAsync(
      const StorePurchaseProperties&) {
    return nullptr;
  }

 private:
  CatalogShape shape_;
};

struct Query : implements<Query, IStoreProductQueryResult> {
  explicit Query(CatalogShape shape) : shape_(shape) {}
  hresult ExtendedError() const {
    return shape_ == CatalogShape::kQueryError ? E_ACCESSDENIED : S_OK;
  }
  IMapView<hstring, StoreProduct> Products() const {
    auto items = single_threaded_map<hstring, StoreProduct>();
    items.Insert(L"missing-entry", nullptr);
    items.Insert(L"test-product",
                 MakeStoreObject<StoreProduct, Product>(shape_));
    return items.GetView();
  }

 private:
  CatalogShape shape_;
};

class CatalogStoreApi final : public StoreApi {
 public:
  explicit CatalogStoreApi(CatalogShape shape) : shape_(shape) {}
  IAsyncOperation<StoreProductQueryResult> GetProductsAsync() override {
    co_await resume_after(5ms);
    co_return MakeStoreObject<StoreProductQueryResult, Query>(shape_);
  }
  IAsyncOperation<StorePurchaseResult> RequestPurchaseAsync(hstring) override {
    return nullptr;
  }
  IAsyncOperation<StoreAppLicense> GetAppLicenseAsync() override {
    return nullptr;
  }

 private:
  CatalogShape shape_;
};
}  // namespace windows_iap::testing
#endif
