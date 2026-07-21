import 'package:flutter_test/flutter_test.dart';
import 'package:windows_store_iap/models/product.dart';
import 'package:windows_store_iap/models/store_license.dart';
import 'package:windows_store_iap/models/store_subscription_info.dart';

void main() {
  group('StoreLicense', () {
    test('converts WinRT ticks using the correct Unix epoch offset', () {
      const license = StoreLicense(
        expirationDate: 116444736000000000,
      );

      expect(license.expiresAt, DateTime.utc(1970));
    });

    test('prefers explicit epoch milliseconds', () {
      final expiration = DateTime.utc(2030, 1, 2, 3, 4, 5);
      final license = StoreLicense(
        expirationDate: 0,
        expirationDateEpochMilliseconds: expiration.millisecondsSinceEpoch,
      );

      expect(license.expiresAt, expiration);
    });

    test('bounds cached entitlement by UTC expiration', () {
      final now = DateTime.utc(2030, 1, 1);
      final active = StoreLicense(
        isEntitled: true,
        expirationDateEpochMilliseconds:
            now.add(const Duration(minutes: 1)).millisecondsSinceEpoch,
      );
      final expired = StoreLicense(
        isEntitled: true,
        expirationDateEpochMilliseconds:
            now.subtract(const Duration(minutes: 1)).millisecondsSinceEpoch,
      );

      expect(active.isCurrentlyEntitled(at: now), isTrue);
      expect(expired.isCurrentlyEntitled(at: now), isFalse);
    });

    test('fails closed when entitlement and expiration are unknown', () {
      const license = StoreLicense();

      expect(license.isCurrentlyEntitled(), isFalse);
    });
  });

  test('parses nested subscription catalog data', () {
    final product = Product.fromJson({
      'title': 'Pro',
      'description': 'Monthly plan',
      'price': r'$9.99',
      'inCollection': true,
      'productKind': 'Durable',
      'storeId': '9ABCDEF12345',
      'skus': [
        {
          'storeId': '9ABCDEF12345/0001',
          'isSubscription': true,
          'subscriptionInfo': {
            'billingPeriod': 1,
            'billingPeriodUnit': 4,
            'hasTrialPeriod': true,
            'trialPeriod': 1,
            'trialPeriodUnit': 3,
          },
        },
      ],
    });

    expect(product.isSubscription, isTrue);
    expect(product.skus.single.subscriptionInfo?.billingPeriod, 1);
    expect(
      product.skus.single.subscriptionInfo?.billingPeriodUnit,
      StoreDurationUnit.month,
    );
    expect(
      product.skus.single.subscriptionInfo?.trialPeriodUnit,
      StoreDurationUnit.week,
    );
  });
}
