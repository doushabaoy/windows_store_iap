import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:windows_store_iap/windows_iap.dart';
import 'package:windows_store_iap/windows_iap_method_channel.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  const channel = MethodChannel('windows_iap_test');
  final platform = MethodChannelWindowsIap(methodChannel: channel);
  final messenger =
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger;

  tearDown(() {
    messenger.setMockMethodCallHandler(channel, null);
  });

  test('maps every purchase status', () async {
    for (var index = 0; index < StorePurchaseStatus.values.length; index++) {
      messenger.setMockMethodCallHandler(channel, (call) async => index);
      expect(
        await platform.makePurchase('9ABCDEF12345'),
        StorePurchaseStatus.values[index],
      );
    }
  });

  test('parses detailed purchase status and extended error', () async {
    MethodCall? captured;
    messenger.setMockMethodCallHandler(channel, (call) async {
      captured = call;
      return {
        'status': StorePurchaseStatus.networkError.index,
        'extendedErrorCode': '0x80072EE7',
      };
    });

    final details = await platform.makePurchaseDetailed('9ABCDEF12345');
    expect(captured?.method, 'makePurchaseDetailed');
    expect(captured?.arguments, {'storeId': '9ABCDEF12345'});
    expect(details.status, StorePurchaseStatus.networkError);
    expect(details.extendedErrorCode, '0x80072EE7');
    expect(details.hasExtendedError, isTrue);
  });

  test('rejects an invalid detailed purchase payload', () async {
    messenger.setMockMethodCallHandler(
      channel,
      (call) async => {'status': 99, 'extendedErrorCode': null},
    );

    await expectLater(
      platform.makePurchaseDetailed('9ABCDEF12345'),
      throwsA(isA<FormatException>()),
    );
  });

  test('parses structured products', () async {
    messenger.setMockMethodCallHandler(
        channel,
        (call) async => [
              {
                'title': 'Pro',
                'description': 'Monthly subscription',
                'price': r'$9.99',
                'inCollection': false,
                'productKind': 'Durable',
                'storeId': '9ABCDEF12345',
                'skus': [
                  {
                    'storeId': '9ABCDEF12345/0001',
                    'isSubscription': true,
                    'availabilities': <Object?>[],
                  },
                ],
              },
            ]);

    final products = await platform.getProducts();
    expect(products.single.storeId, '9ABCDEF12345');
    expect(products.single.isSubscription, isTrue);
  });

  test('parses structured add-on licenses', () async {
    messenger.setMockMethodCallHandler(
        channel,
        (call) async => {
              '9ABCDEF12345': {
                'isActive': true,
                'isEntitled': true,
                'skuStoreId': '9ABCDEF12345/0001',
                'productStoreId': '9ABCDEF12345',
                'expirationDateEpochMilliseconds': 1893456000000,
              },
            });

    final licenses = await platform.getAddonLicenses();
    expect(licenses.values.single.productStoreId, '9ABCDEF12345');
    expect(licenses.values.single.expiresAt, DateTime.utc(2030));
  });

  test('parses app license and nested add-ons', () async {
    messenger.setMockMethodCallHandler(
        channel,
        (call) async => {
              'isActive': true,
              'isTrial': false,
              'skuStoreId': 'APP/0001',
              'addOnLicenses': {
                '9ABCDEF12345': {
                  'isEntitled': true,
                  'skuStoreId': '9ABCDEF12345/0001',
                  'expirationDateEpochMilliseconds': 1893456000000,
                },
              },
            });

    final license = await platform.getAppLicense();
    expect(license.isActive, isTrue);
    expect(license.addOnLicenses, hasLength(1));
  });

  test('forwards storeId when checking an entitlement', () async {
    MethodCall? captured;
    messenger.setMockMethodCallHandler(channel, (call) async {
      captured = call;
      return true;
    });

    expect(
      await platform.checkPurchase(storeId: '9ABCDEF12345'),
      isTrue,
    );
    expect(captured?.method, 'checkPurchase');
    expect(captured?.arguments, {'storeId': '9ABCDEF12345'});
  });
}
