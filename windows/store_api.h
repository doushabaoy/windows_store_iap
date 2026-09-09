#ifndef WINDOWS_STORE_IAP_STORE_API_H_
#define WINDOWS_STORE_IAP_STORE_API_H_

#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Services.Store.h>

#include <functional>
#include <memory>

namespace windows_iap {

// The Store boundary is injectable so tests can complete real WinRT operations
// after the Dart method handler has returned, without contacting the Store.
class StoreApi {
 public:
  virtual ~StoreApi() = default;
  virtual winrt::Windows::Foundation::IAsyncOperation<
      winrt::Windows::Services::Store::StorePurchaseResult>
  RequestPurchaseAsync(winrt::hstring store_id) = 0;
  virtual winrt::Windows::Foundation::IAsyncOperation<
      winrt::Windows::Services::Store::StoreProductQueryResult>
  GetProductsAsync() = 0;
  virtual winrt::Windows::Foundation::IAsyncOperation<
      winrt::Windows::Services::Store::StoreAppLicense>
  GetAppLicenseAsync() = 0;
};

using StoreApiFactory = std::function<std::shared_ptr<StoreApi>(HWND)>;
std::shared_ptr<StoreApi> CreateStoreApi(HWND window);

// Called on the platform thread for each request, after the view is parented.
HWND ResolveStoreWindow(HWND view_window);

}  // namespace windows_iap

#endif  // WINDOWS_STORE_IAP_STORE_API_H_
