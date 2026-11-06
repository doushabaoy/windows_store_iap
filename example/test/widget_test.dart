import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:windows_store_iap/windows_store_iap.dart';
import 'package:windows_store_iap/windows_iap_method_channel.dart';
import 'package:windows_store_iap/windows_iap_platform_interface.dart';
import 'package:windows_store_iap_example/main.dart';

void main() {
  testWidgets('shows Store operations', (tester) async {
    await tester.pumpWidget(const MyApp());

    expect(find.text('checkPurchase'), findsOneWidget);
    expect(find.text('makePurchase'), findsOneWidget);
    expect(find.text('getProducts'), findsOneWidget);
    expect(find.text('getAddonLicenses'), findsOneWidget);
    expect(find.text('getAppLicense'), findsOneWidget);
    expect(find.text('检查订阅'), findsOneWidget);
    expect(find.text('购买订阅'), findsOneWidget);
    expect(find.text('评价应用'), findsOneWidget);
  });

  testWidgets('review disables repeat clicks and displays the result', (
    tester,
  ) async {
    final previous = WindowsIapPlatform.instance;
    const channel = MethodChannel('example_review_test');
    WindowsIapPlatform.instance = MethodChannelWindowsIap(
      methodChannel: channel,
    );
    final messenger =
        TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger;
    addTearDown(() {
      WindowsIapPlatform.instance = previous;
      messenger.setMockMethodCallHandler(channel, null);
    });
    final completion = Completer<Map<String, Object?>>();
    var calls = 0;
    messenger.setMockMethodCallHandler(channel, (call) async {
      calls++;
      expect(call.method, 'requestReview');
      return completion.future;
    });
    await tester.pumpWidget(const MyApp());
    await tester.tap(find.text('评价应用'));
    await tester.pump();
    expect(find.byType(CircularProgressIndicator), findsOneWidget);
    for (final button in tester.widgetList<ElevatedButton>(
      find.byType(ElevatedButton),
    )) {
      expect(button.onPressed, isNull);
    }
    completion.complete({
      'status': StoreReviewStatus.succeeded.index,
      'updatedExistingReview': false,
      'extendedJsonData': '{"status":"success","data":{"updated":false}}',
    });
    await tester.pumpAndSettle();
    expect(calls, 1);
    expect(find.byType(CircularProgressIndicator), findsNothing);
    expect(find.textContaining('review: succeeded'), findsWidgets);
    expect(find.textContaining('updatedExistingReview: false'), findsWidgets);
  });

  testWidgets('review shows PlatformException details', (tester) async {
    final previous = WindowsIapPlatform.instance;
    const channel = MethodChannel('example_review_error_test');
    WindowsIapPlatform.instance = MethodChannelWindowsIap(
      methodChannel: channel,
    );
    final messenger =
        TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger;
    addTearDown(() {
      WindowsIapPlatform.instance = previous;
      messenger.setMockMethodCallHandler(channel, null);
    });
    messenger.setMockMethodCallHandler(channel, (call) async {
      throw PlatformException(
        code: '0x80070578',
        message: 'Invalid window',
        details: 'No Flutter view',
      );
    });
    await tester.pumpWidget(const MyApp());
    await tester.tap(find.text('评价应用'));
    await tester.pumpAndSettle();
    expect(find.textContaining('0x80070578'), findsWidgets);
    expect(find.textContaining('No Flutter view'), findsWidgets);
  });
}
