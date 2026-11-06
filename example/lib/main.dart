import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:windows_store_iap/windows_store_iap.dart';
import 'package:windows_store_iap_example/app_logger.dart';

void main() {
  runApp(const MyApp());
}

const productStoreId = 'StoreId';

class MyApp extends StatefulWidget {
  const MyApp({Key? key}) : super(key: key);

  @override
  State<MyApp> createState() => _MyAppState();
}

class _MyAppState extends State<MyApp> {
  final _windowsIapPlugin = WindowsStoreIap();
  bool _busy = false;
  String? _lastResult;

  Future<void> _run(
    BuildContext context,
    String operation,
    Future<String> Function() action,
  ) async {
    if (_busy) return;
    setState(() => _busy = true);

    late final String message;
    try {
      message = await action();
    } on PlatformException catch (error) {
      message = '$operation failed (${error.code}): '
          '${error.message ?? 'Microsoft Store error'}\n'
          'details: ${error.details ?? '(none)'}';
    } on Object catch (error) {
      message = '$operation failed: $error';
    }

    if (!context.mounted) return;
    setState(() {
      _busy = false;
      _lastResult = message;
    });
    ScaffoldMessenger.of(
      context,
    ).showSnackBar(SnackBar(content: Text(message)));
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      home: Builder(
        builder: (context) {
          return Scaffold(
            appBar: AppBar(title: const Text('Plugin example app')),
            body: SingleChildScrollView(
              padding: const EdgeInsets.all(24),
              child: Center(
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    ElevatedButton(
                      onPressed: _busy
                          ? null
                          : () => _run(context, 'requestReview', () async {
                                final result =
                                    await _windowsIapPlugin.requestReview();
                                return 'review: ${result.status.name}\n'
                                    'updatedExistingReview: '
                                    '${result.updatedExistingReview ?? 'unknown'}\n'
                                    'extendedErrorCode: '
                                    '${result.extendedErrorCode ?? '(none)'}\n'
                                    'extendedJsonData: '
                                    '${result.extendedJsonData ?? '(none)'}';
                              }),
                      child: const Text('评价应用'),
                    ),
                    if (_busy) ...[
                      const SizedBox(height: 16),
                      const CircularProgressIndicator(),
                    ],
                    if (_lastResult != null) ...[
                      const SizedBox(height: 16),
                      SelectableText(_lastResult!),
                    ],
                    const SizedBox(height: 16),
                    ElevatedButton(
                      onPressed: _busy
                          ? null
                          : () => _run(context, 'checkPurchase', () async {
                                final result = await _windowsIapPlugin
                                    .checkPurchase(storeId: productStoreId);
                                return 'checkPurchase: $result';
                              }),
                      child: const Text('checkPurchase'),
                    ),
                    const SizedBox(height: 16),
                    ElevatedButton(
                      onPressed: _busy
                          ? null
                          : () => _run(context, 'makePurchase', () async {
                                final result = await _windowsIapPlugin
                                    .makePurchaseDetailed(productStoreId);
                                final code = result.extendedErrorCode == null
                                    ? ''
                                    : ' (${result.extendedErrorCode})';
                                return 'purchase: ${result.status.name}$code';
                              }),
                      child: const Text('makePurchase'),
                    ),
                    const SizedBox(height: 16),
                    ElevatedButton(
                      onPressed: _busy
                          ? null
                          : () => _run(context, 'getProducts', () async {
                                final products =
                                    await _windowsIapPlugin.getProducts();
                                return 'products: ${products.length}';
                              }),
                      child: const Text('getProducts'),
                    ),
                    const SizedBox(height: 16),
                    ElevatedButton(
                      onPressed: _busy
                          ? null
                          : () => _run(context, 'getAddonLicenses', () async {
                                final result =
                                    await _windowsIapPlugin.getAddonLicenses();
                                return 'add-on licenses: ${result.length}';
                              }),
                      child: const Text('getAddonLicenses'),
                    ),
                    const SizedBox(height: 16),
                    ElevatedButton(
                      onPressed: _busy
                          ? null
                          : () => _run(context, 'getAppLicense', () async {
                                final license =
                                    await _windowsIapPlugin.getAppLicense();
                                return 'app active: ${license.isActive}, '
                                    'add-ons: ${license.addOnLicenses.length}';
                              }),
                      child: const Text('getAppLicense'),
                    ),
                    const SizedBox(height: 16),
                    ElevatedButton(
                      onPressed: _busy
                          ? null
                          : () =>
                              _run(context, 'inspectSubscription', () async {
                                await inspectSubscription(productStoreId);
                                return 'subscription inspection completed';
                              }),
                      child: const Text('检查订阅'),
                    ),
                    const SizedBox(height: 16),
                    ElevatedButton(
                      onPressed: _busy
                          ? null
                          : () => _run(context, '购买订阅', () async {
                                final result = await store.makePurchaseDetailed(
                                  productStoreId,
                                );
                                if (result.status ==
                                        StorePurchaseStatus.succeeded ||
                                    result.status ==
                                        StorePurchaseStatus.alreadyPurchased) {
                                  await inspectSubscription(productStoreId);
                                }
                                final code = result.extendedErrorCode == null
                                    ? ''
                                    : ' (${result.extendedErrorCode})';
                                return 'purchase: ${result.status.name}$code';
                              }),
                      child: const Text('购买订阅'),
                    ),
                  ],
                ),
              ),
            ),
          );
        },
      ),
    );
  }
}
