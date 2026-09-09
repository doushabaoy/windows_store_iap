import 'package:flutter_test/flutter_test.dart';
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
  });
}
