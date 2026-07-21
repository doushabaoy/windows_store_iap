import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

import 'src/utils.dart';
import 'windows_iap.dart';
import 'windows_iap_platform_interface.dart';

/// MethodChannel implementation for Microsoft Store on Windows.
class MethodChannelWindowsIap extends WindowsIapPlatform {
  /// Channel used to communicate with the Windows plugin.
  @visibleForTesting
  final MethodChannel methodChannel;

  /// Creates a MethodChannel implementation.
  ///
  /// Supplying [methodChannel] is primarily useful for tests.
  MethodChannelWindowsIap({
    this.methodChannel = const MethodChannel('windows_store_iap'),
  });

  @override
  Future<StorePurchaseStatus?> makePurchase(String storeId) async {
    final result = await methodChannel.invokeMethod<int>(
      'makePurchase',
      {'storeId': storeId},
    );
    if (result == null || result < 0 || result > 4) return null;
    return StorePurchaseStatus.values[result];
  }

  @override
  Future<List<Product>> getProducts() async {
    final result = await methodChannel.invokeMethod<dynamic>('getProducts');
    if (result == null) return const [];

    final dynamic decoded = result is String ? jsonDecode(result) : result;
    if (decoded is! List) {
      throw const FormatException(
        'Microsoft Store returned an invalid product payload.',
      );
    }
    return decoded
        .map((item) => Product.fromJson(asStringKeyedMap(item)))
        .toList(growable: false);
  }

  @override
  Future<bool> checkPurchase({required String storeId}) async {
    final result = await methodChannel.invokeMethod<bool>(
      'checkPurchase',
      {'storeId': storeId},
    );
    return result ?? false;
  }

  @override
  Future<Map<String, StoreLicense>> getAddonLicenses() async {
    final result =
        await methodChannel.invokeMethod<dynamic>('getAddonLicenses');
    if (result == null) return const {};

    final dynamic decoded = result is String ? jsonDecode(result) : result;
    final licenses = asStringKeyedMap(decoded);
    return licenses.map((key, value) {
      final dynamic licenseValue = value is String ? jsonDecode(value) : value;
      return MapEntry(
        key,
        StoreLicense.fromJson(asStringKeyedMap(licenseValue)),
      );
    });
  }

  @override
  Future<StoreAppLicense> getAppLicense() async {
    final result = await methodChannel.invokeMethod<dynamic>('getAppLicense');
    if (result == null) {
      throw const FormatException(
        'Microsoft Store returned an empty app license payload.',
      );
    }
    final dynamic decoded = result is String ? jsonDecode(result) : result;
    return StoreAppLicense.fromJson(asStringKeyedMap(decoded));
  }
}
