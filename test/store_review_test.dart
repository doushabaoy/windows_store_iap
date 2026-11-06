import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:windows_store_iap/windows_store_iap.dart';
import 'package:windows_store_iap/windows_iap_method_channel.dart';
import 'package:windows_store_iap/windows_iap_platform_interface.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  const channel = MethodChannel('windows_review_test');
  final platform = MethodChannelWindowsIap(methodChannel: channel);
  final messenger =
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger;

  tearDown(() => messenger.setMockMethodCallHandler(channel, null));

  for (final updated in [false, true]) {
    test('successful review, updatedExistingReview=$updated', () async {
      final json = '{"status":"success","data":{"updated":$updated}}';
      MethodCall? captured;
      messenger.setMockMethodCallHandler(channel, (call) async {
        captured = call;
        return {
          'status': 0,
          'updatedExistingReview': updated,
          'wasUpdated': !updated,
          'extendedErrorCode': null,
          'extendedJsonData': json,
        };
      });

      final result = await platform.requestReview();
      expect(captured?.method, 'requestReview');
      expect(captured?.arguments, isNull);
      expect(result.status, StoreReviewStatus.succeeded);
      expect(result.updatedExistingReview, updated);
      expect(result.extendedErrorCode, isNull);
      expect(result.extendedJsonData, json);
    });
  }

  for (final status in StoreReviewStatus.values.skip(1)) {
    test('preserves ${status.name} and HRESULT without throwing', () async {
      messenger.setMockMethodCallHandler(
        channel,
        (call) async => {
          'status': status.index,
          'updatedExistingReview': null,
          'extendedErrorCode': '0x80072EE7',
          'extendedJsonData': '{"status":"aborted"}',
        },
      );
      final result = await platform.requestReview();
      expect(result.status, status);
      expect(result.updatedExistingReview, isNull);
      expect(result.extendedErrorCode, '0x80072EE7');
    });
  }

  for (final json in <String?>[null, '', '{invalid', '{}']) {
    test('preserves missing or invalid JSON: $json', () async {
      messenger.setMockMethodCallHandler(
        channel,
        (call) async => {
          'status': 0,
          'updatedExistingReview': null,
          'wasUpdated': true,
          'extendedJsonData': json,
        },
      );
      final result = await platform.requestReview();
      expect(result.status, StoreReviewStatus.succeeded);
      expect(result.updatedExistingReview, isNull);
      expect(result.extendedJsonData, json);
    });
  }

  test(
    'non-success result cannot claim to have updated an existing review',
    () {
      final result = StoreReviewResult.fromJson({
        'status': StoreReviewStatus.error.index,
        'updatedExistingReview': true,
      });
      expect(result.updatedExistingReview, isNull);
    },
  );

  for (final payload in <Object?>[
    null,
    [],
    {},
    {'status': -1},
    {'status': 4},
    {'status': '0'},
    {'status': 0, 'updatedExistingReview': 'false'},
    {'status': 0, 'extendedErrorCode': 5},
    {'status': 0, 'extendedJsonData': {}},
  ]) {
    test('rejects invalid review channel payload: $payload', () async {
      messenger.setMockMethodCallHandler(channel, (call) async => payload);
      await expectLater(
        platform.requestReview(),
        throwsA(isA<FormatException>()),
      );
    });
  }

  for (final code in [
    'review_in_progress',
    'purchase_in_progress',
    'plugin_disposed',
    '0x80070578',
    '0x80004001',
    '0x80070005',
    'native_error',
  ]) {
    test('propagates native failure: $code', () async {
      messenger.setMockMethodCallHandler(channel, (call) async {
        throw PlatformException(code: code, message: 'Store failure');
      });
      await expectLater(
        platform.requestReview(),
        throwsA(isA<PlatformException>().having((e) => e.code, 'code', code)),
      );
    });
  }

  test('both public entry points delegate requestReview', () async {
    final previous = WindowsIapPlatform.instance;
    WindowsIapPlatform.instance = platform;
    addTearDown(() => WindowsIapPlatform.instance = previous);
    var calls = 0;
    messenger.setMockMethodCallHandler(channel, (call) async {
      calls++;
      return {'status': 0};
    });
    expect(
      (await WindowsStoreIap().requestReview()).status,
      StoreReviewStatus.succeeded,
    );
    expect(
      (await WindowsIap().requestReview()).status,
      StoreReviewStatus.succeeded,
    );
    expect(calls, 2);
  });
}
