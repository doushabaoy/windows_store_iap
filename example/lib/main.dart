// ignore_for_file: use_build_context_synchronously, avoid_print

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

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      home: Builder(builder: (context) {
        return Scaffold(
          appBar: AppBar(
            title: const Text('Plugin example app'),
          ),
          body: Center(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                ElevatedButton(
                    onPressed: () async {
                      final result = await _windowsIapPlugin.checkPurchase(
                        storeId: productStoreId,
                      );
                      if (!mounted) return;
                      ScaffoldMessenger.of(context).showSnackBar(
                        SnackBar(content: Text('checkPurchase: $result')),
                      );
                    },
                    child: const Text('checkPurchase')),
                const SizedBox(height: 16),
                ElevatedButton(
                    onPressed: () async {
                      final result =
                          await _windowsIapPlugin.makePurchase(productStoreId);
                      print('result is $result');
                    },
                    child: const Text('makePurchase')),
                const SizedBox(height: 16),
                ElevatedButton(
                    onPressed: () async {
                      try {
                        final products = await _windowsIapPlugin.getProducts();
                        print('products: $products');
                      } on PlatformException catch (e) {
                        print('error');
                        print(e.toString());
                      }
                    },
                    child: const Text('getProducts')),
                const SizedBox(height: 16),
                ElevatedButton(
                    onPressed: () async {
                      final result = await _windowsIapPlugin.getAddonLicenses();
                      print('licenses: $result');
                    },
                    child: const Text('getAddonLicenses')),
                const SizedBox(height: 16),
                ElevatedButton(
                    onPressed: () async {
                      final license = await _windowsIapPlugin.getAppLicense();
                      print('app license: ${license.toJson()}');
                    },
                    child: const Text('getAppLicense')),
                const SizedBox(height: 16),
                ElevatedButton(
                  onPressed: () async {
                    await inspectSubscription(productStoreId);
                  },
                  child: const Text('检查订阅'),
                ),
                const SizedBox(height: 16),
                ElevatedButton(
                  onPressed: () async {
                    try {
                      final status = await store.makePurchase(productStoreId);
                      print('purchase status: $status');

                      // 购买完成后重新读取许可证。
                      await inspectSubscription(productStoreId);
                    } on PlatformException catch (error) {
                      print('code: ${error.code}');
                      print('message: ${error.message}');
                      print('details: ${error.details}');
                    }
                  },
                  child: const Text('购买订阅'),
                )
              ],
            ),
          ),
        );
      }),
    );
  }
}
