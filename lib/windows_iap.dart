/// Microsoft Store purchases, product data, and subscription entitlements for
/// Flutter applications running on Windows.
///
/// Prefer importing `package:windows_store_iap/windows_store_iap.dart` in new
/// applications. This library remains available for source compatibility with
/// the original package.
library windows_iap;

import 'dart:io';

import 'models/product.dart';
import 'models/store_app_license.dart';
import 'models/store_license.dart';
import 'windows_iap_platform_interface.dart';

export 'models/product.dart';
export 'models/store_app_license.dart';
export 'models/store_availability.dart';
export 'models/store_collection_data.dart';
export 'models/store_license.dart';
export 'models/store_price.dart';
export 'models/store_sku.dart';
export 'models/store_subscription_info.dart';

/// Result returned by the Microsoft Store purchase UI.
enum StorePurchaseStatus {
  /// The purchase completed successfully.
  succeeded,

  /// The user already owns the requested durable product.
  alreadyPurchased,

  /// The purchase was cancelled or otherwise not completed.
  notPurchased,

  /// The Store could not complete the purchase because of a network error.
  networkError,

  /// The Store service returned an error.
  serverError,
}

/// Client API for Microsoft Store purchases and license information.
///
/// This API can only be called from a packaged Windows application associated
/// with a Microsoft Store product. For authoritative commercial subscription
/// state, validate the entitlement on your server as well.
class WindowsIap {
  void _ensureWindows() {
    if (!Platform.isWindows) {
      throw UnsupportedError(
        'windows_store_iap is only available on Windows.',
      );
    }
  }

  /// Opens the Microsoft Store purchase UI for [storeId].
  ///
  /// [storeId] must be a non-empty Product Store ID. A `null` result means the
  /// native Store returned an unknown status value.
  Future<StorePurchaseStatus?> makePurchase(String storeId) {
    _ensureWindows();
    if (storeId.trim().isEmpty) {
      throw ArgumentError.value(storeId, 'storeId', 'Must not be empty.');
    }
    return WindowsIapPlatform.instance.makePurchase(storeId.trim());
  }

  /// Gets the associated Microsoft Store products, including SKU,
  /// subscription period, price, trial and availability data.
  Future<List<Product>> getProducts() {
    _ensureWindows();
    return WindowsIapPlatform.instance.getProducts();
  }

  /// Performs a best-effort client-side entitlement check.
  ///
  /// Microsoft Store add-on [StoreLicense.isActive] is not used because the
  /// native property currently always returns true. A license must be present
  /// in AddOnLicenses and its UTC expiration time must still be in the future.
  /// An empty [storeId] checks whether any add-on is currently entitled.
  ///
  /// For authoritative auto-renewal, refund, revocation and dunning state,
  /// verify the subscription with Microsoft's server-side Recurrence API.
  Future<bool> checkPurchase({String storeId = ''}) {
    _ensureWindows();
    return WindowsIapPlatform.instance.checkPurchase(
      storeId: storeId.trim(),
    );
  }

  /// Returns the add-on licenses currently reported by Microsoft Store.
  ///
  /// Map keys are the identifiers supplied by `StoreAppLicense.AddOnLicenses`.
  /// Use [StoreLicense.isCurrentlyEntitled] instead of `isActive` when making a
  /// client-side entitlement decision.
  Future<Map<String, StoreLicense>> getAddonLicenses() {
    _ensureWindows();
    return WindowsIapPlatform.instance.getAddonLicenses();
  }

  /// Gets the current app license and its add-on license collection.
  ///
  /// The Windows API can return cached information while offline. Treat this
  /// result as a client-side convenience, not as a server-authoritative record.
  Future<StoreAppLicense> getAppLicense() {
    _ensureWindows();
    return WindowsIapPlatform.instance.getAppLicense();
  }
}
