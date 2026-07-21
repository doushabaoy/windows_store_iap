import 'dart:convert';

import 'package:flutter/services.dart';
import 'package:windows_store_iap/windows_store_iap.dart';

final store = WindowsStoreIap();

Future<void> inspectSubscription(String storeId) async {
  try {
    final products = await store.getProducts();

    print('======= PRODUCTS =======');
    for (final product in products) {
      print(const JsonEncoder.withIndent('  ').convert(product.toJson()));
    }

    final appLicense = await store.getAppLicense();

    print('======= APP LICENSE =======');
    print(
      const JsonEncoder.withIndent('  ').convert(appLicense.toJson()),
    );

    final licenses = await store.getAddonLicenses();

    print('======= ADD-ON LICENSES =======');
    for (final entry in licenses.entries) {
      print('collectionKey: ${entry.key}');
      print(
        const JsonEncoder.withIndent('  ').convert(entry.value.toJson()),
      );
      print('expiresAt: ${entry.value.expiresAt}');
      print(
        'currentlyEntitled: ${entry.value.isCurrentlyEntitled()}',
      );
    }

    final entitled = await store.checkPurchase(storeId: storeId);
    print('======= RESULT =======');
    print('storeId: $storeId');
    print('entitled: $entitled');
  } on PlatformException catch (error, stackTrace) {
    print('Microsoft Store 调用失败');
    print('code: ${error.code}');
    print('message: ${error.message}');
    print('details: ${error.details}');
    print(stackTrace);
  }
}
