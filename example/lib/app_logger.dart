import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:windows_store_iap/windows_store_iap.dart';

final store = WindowsStoreIap();

Future<void> inspectSubscription(String storeId) async {
  try {
    final products = await store.getProducts();

    debugPrint('======= PRODUCTS =======');
    for (final product in products) {
      debugPrint(
        'storeId=${product.storeId}, title=${product.title}, '
        'price=${product.price}, subscription=${product.isSubscription}',
      );
    }

    final appLicense = await store.getAppLicense();

    debugPrint('======= APP LICENSE =======');
    debugPrint(
      'active=${appLicense.isActive}, trial=${appLicense.isTrial}, '
      'expiresAt=${appLicense.expiresAt}, '
      'addOnCount=${appLicense.addOnLicenses.length}',
    );

    final licenses = await store.getAddonLicenses();

    debugPrint('======= ADD-ON LICENSES =======');
    for (final entry in licenses.entries) {
      debugPrint(
        'productStoreId=${entry.value.productStoreId}, '
        'expiresAt=${entry.value.expiresAt}, '
        'currentlyEntitled=${entry.value.isCurrentlyEntitled()}',
      );
    }

    final entitled = await store.checkPurchase(storeId: storeId);
    debugPrint('======= RESULT =======');
    debugPrint('storeId=$storeId, entitled=$entitled');
  } on PlatformException catch (error) {
    debugPrint(
      'Microsoft Store call failed: ${error.code}: '
      '${error.message ?? 'unknown error'}',
    );
  } on Object catch (error) {
    debugPrint('Subscription inspection failed: $error');
  }
}
