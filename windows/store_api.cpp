#include "store_api.h"

#include <shobjidl.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace windows_iap {
namespace {

class WindowsStoreApi final : public StoreApi {
 public:
  explicit WindowsStoreApi(HWND window)
      : store_(winrt::Windows::Services::Store::StoreContext::GetDefault()) {
    if (!store_) {
      throw winrt::hresult_error(E_UNEXPECTED, L"StoreContext is unavailable.");
    }
    winrt::check_hresult(
        store_.as<IInitializeWithWindow>()->Initialize(window));
  }

  winrt::Windows::Foundation::IAsyncOperation<
      winrt::Windows::Services::Store::StorePurchaseResult>
  RequestPurchaseAsync(winrt::hstring store_id) override {
    return store_.RequestPurchaseAsync(store_id);
  }

  winrt::Windows::Foundation::IAsyncOperation<
      winrt::Windows::Services::Store::StoreProductQueryResult>
  GetProductsAsync() override {
    return store_.GetAssociatedStoreProductsAsync(
        {L"Consumable", L"Durable", L"UnmanagedConsumable"});
  }

  winrt::Windows::Foundation::IAsyncOperation<
      winrt::Windows::Services::Store::StoreAppLicense>
  GetAppLicenseAsync() override {
    return store_.GetAppLicenseAsync();
  }

 private:
  winrt::Windows::Services::Store::StoreContext store_;
};

}  // namespace

HWND ResolveStoreWindow(HWND view_window) {
  if (!view_window || !::IsWindow(view_window)) {
    return nullptr;
  }
  const HWND root = ::GetAncestor(view_window, GA_ROOT);
  return root && ::IsWindow(root) ? root : nullptr;
}

std::shared_ptr<StoreApi> CreateStoreApi(HWND window) {
  if (!window || !::IsWindow(window)) {
    throw winrt::hresult_error(
        HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE),
        L"A valid Flutter window is required for Microsoft Store operations.");
  }
  return std::make_shared<WindowsStoreApi>(window);
}

}  // namespace windows_iap
