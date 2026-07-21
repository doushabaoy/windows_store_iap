# windows_store_iap

面向 Windows 桌面 Flutter 应用的 Microsoft Store 内购与订阅插件。

这是基于 `windows_iap` 的增强分支，重点修复订阅取消后仍长期被判定为会员的问题，并补充结构化许可证、SKU、价格和订阅数据。原项目版权与 BSD-3-Clause 许可证声明保留在 [LICENSE](LICENSE) 中；本项目与原作者不存在背书关系。

## 安装

```yaml
dependencies:
  windows_store_iap: ^0.1.0
```

```dart
import 'package:windows_store_iap/windows_store_iap.dart';
```

完整字段、返回值和异常说明见 [API 文档](doc/API.md)。

## 能力

- 购买 Durable、Consumable 和 UnmanagedConsumable 商品。
- 获取结构化 Product、SKU、Availability、Price、SubscriptionInfo 数据。
- 获取应用许可证、Add-on 许可证、试用信息和 UTC 到期时间。
- 使用 AddOnLicenses 存在性和 ExpirationDate 判断客户端订阅权益。
- MethodChannel 使用结构化数据，避免手工拼接 JSON 导致转义或解析错误。
- 提供 `makePurchase`、`getProducts`、`checkPurchase`、`getAddonLicenses`、`getAppLicense` API。
- 保留 `WindowsIap` 兼容类，并为新项目提供 `WindowsStoreIap` 入口。

## 订阅语义

Microsoft 文档说明，Add-on 的 `StoreLicense.IsActive` 当前始终返回 `true`。本插件不再使用该字段判断会员资格，而是要求许可证仍存在于 `StoreAppLicense.AddOnLicenses`，并且 `ExpirationDate` 晚于当前 UTC 时间。

用户关闭自动续费后，在已经付费的当前计费周期内仍然具有权益；只有计费周期结束后才应失去权益。

`GetAppLicenseAsync` 在离线时可能返回设备缓存。本插件使用到期时间限制过期缓存，但客户端时钟和网络均不可信。退款、撤销、自动续费、扣款重试和宽限期等商业关键状态，必须在你自己的服务端使用 Microsoft Store Recurrence/Collections API 校验。

## 使用

```dart
final store = WindowsStoreIap();

final products = await store.getProducts();
for (final product in products.where((item) => item.isSubscription)) {
  for (final sku in product.skus.where((item) => item.isSubscription == true)) {
    final info = sku.subscriptionInfo;
    print('${info?.billingPeriod} ${info?.billingPeriodUnit}');
    print(sku.price?.formattedRecurrencePrice);
  }
}

final entitled = await store.checkPurchase(
  storeId: 'YOUR_12_CHARACTER_PRODUCT_STORE_ID',
);

final appLicense = await store.getAppLicense();
for (final license in appLicense.addOnLicenses.values) {
  print('${license.productStoreId}: ${license.expiresAt}');
  print(license.isCurrentlyEntitled());
}
```

`checkPurchase` 同时接受 Product Store ID、完整 SKU Store ID、许可证集合 key 或 In-app offer token。推荐传 Partner Center 中的 12 位 Product Store ID。

客户端检查的含义是“许可证存在且尚未到期”，不能直接说明自动续费仍开启。用户取消自动续费后，在已支付周期结束前继续是会员属于正常行为；到期后插件会返回 `false`。

## 上线要求

- 应用和 Add-on 必须在 Partner Center 正确关联并发布。
- 测试设备至少需要从 Microsoft Store 安装一次应用以建立许可证上下文。
- 内购 API 不能在管理员权限/提升权限进程中使用。
- 商业权益应绑定你的业务账号，并由后端保存 Microsoft 订阅状态；不要仅信任客户端返回的布尔值。
- 服务端密钥、Entra ID 凭据和 User Store ID key 不得写入客户端。
- 服务端校验不能安全地内置在公开 Flutter 包中；本包只负责采集客户端 Store 数据并提供给你的业务层。

## 主要模型

- `StoreAppLicense`: 应用许可证、试用状态、Add-on 许可证集合。
- `StoreLicense`: Product/SKU ID、到期时间、权益辅助判断、完整许可证 JSON。
- `Product` / `StoreSku`: 商品、SKU、订阅周期和用户 collection 数据。
- `StorePrice`: 币种、基础价、当前价、续订价和促销信息。
- `StoreAvailability`: SKU 可售实例、价格和结束时间。

## Microsoft 文档

- [StoreLicense.IsActive](https://learn.microsoft.com/en-us/uwp/api/windows.services.store.storelicense.isactive)
- [GetAppLicenseAsync](https://learn.microsoft.com/en-us/uwp/api/windows.services.store.storecontext.getapplicenseasync)
- [Enable subscription add-ons](https://learn.microsoft.com/en-us/windows/uwp/monetize/enable-subscription-add-ons-for-your-app)
- [Microsoft Store Recurrence Query](https://learn.microsoft.com/en-us/gaming/gdk/docs/store/commerce/service-to-service/microsoft-store-apis/xstore-v8-recurrence-query)
