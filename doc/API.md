# windows_store_iap API

本文档对应 `windows_store_iap 0.1.0`。所有时间均以 UTC 表示；序列化后的时间字段使用 Unix epoch 毫秒。

## 导入和入口

```dart
import 'package:flutter/services.dart';
import 'package:windows_store_iap/windows_store_iap.dart';

final store = WindowsStoreIap();
```

`WindowsIap` 与 `WindowsStoreIap` 提供相同能力。前者用于兼容原 `windows_iap` 源码，新的应用建议使用 `WindowsStoreIap`。

插件只支持 Windows。其他平台调用会抛出 `UnsupportedError`。应用必须以 MSIX 等形式关联 Microsoft Store 产品；未建立 Store 许可证上下文时，原生 API 可能返回错误。

## WindowsStoreIap

### makePurchase

```dart
Future<StorePurchaseStatus?> makePurchase(String storeId)
```

打开 Microsoft Store 购买界面。`storeId` 必须是非空的 Product Store ID，否则抛出 `ArgumentError`。

返回值：

| 值 | 含义 |
| --- | --- |
| `succeeded` | 购买成功 |
| `alreadyPurchased` | 已拥有该 Durable 商品 |
| `notPurchased` | 用户取消或购买未完成 |
| `networkError` | 网络错误 |
| `serverError` | Microsoft Store 服务错误 |
| `null` | 原生端返回了当前版本不认识的状态值 |

Flutter `PlatformException` 会保留原生 HRESULT 和错误信息，调用方应捕获并记录 `code`、`message`、`details`。

### getProducts

```dart
Future<List<Product>> getProducts()
```

获取当前 Store 应用关联的商品。每个 `Product` 包含 Product Store ID、商品类型、价格、SKU、订阅周期、试用、Availability 和用户 collection 数据。没有商品时返回空列表。

### checkPurchase

```dart
Future<bool> checkPurchase({String storeId = ''})
```

执行客户端权益检查。匹配以下任一标识：

- Product Store ID（推荐）；
- 完整 SKU Store ID；
- `AddOnLicenses` 集合 key；
- In-app offer token。

传空字符串时，只要任一 Add-on 许可证当前有效就返回 `true`。有效条件为许可证仍在 `AddOnLicenses` 中，且其 UTC 到期时间晚于当前时间。插件不会将 `StoreLicense.isActive` 作为会员依据，因为 Microsoft 文档说明 Add-on 的该属性当前始终为 `true`。

这是客户端便捷检查。离线缓存、设备时钟修改、退款、撤销、扣款重试、宽限期和自动续费开关必须由业务服务端处理。

### getAddonLicenses

```dart
Future<Map<String, StoreLicense>> getAddonLicenses()
```

返回 `StoreAppLicense.AddOnLicenses` 的结构化映射。使用 `StoreLicense.isCurrentlyEntitled()` 判断单个许可证当前是否有效。

### getAppLicense

```dart
Future<StoreAppLicense> getAppLicense()
```

返回应用许可证、试用信息和 Add-on 许可证集合。离线时 Windows 可能返回缓存数据，因此不应将它作为商业服务端的唯一凭据。

## 数据模型

### Product

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `title` / `description` | `String?` | 本地化标题和描述 |
| `price` | `String?` | 兼容旧 API 的格式化默认价格 |
| `inCollection` | `bool?` | 是否已有 SKU 在用户 collection 中 |
| `productKind` | `String?` | Durable、Consumable、UnmanagedConsumable 等商品类型 |
| `storeId` | `String?` | Product Store ID |
| `inAppOfferToken` | `String?` | 开发者配置的 offer token |
| `language` | `String?` | Store 本地化语言 |
| `storePrice` | `StorePrice?` | Product 级价格 |
| `skus` | `List<StoreSku>` | SKU 列表 |
| `extendedJsonData` | `String?` | Microsoft Store 原始扩展 JSON |

`isSubscription` 在任一 SKU 为订阅时返回 `true`。

### StoreSku

包含 `storeId`、本地化标题/描述/语言、`isInUserCollection`、`isSubscription`、`isTrial`、`customDeveloperData`、`price`、`subscriptionInfo`、`collectionData`、`availabilities` 和 `extendedJsonData`。

### StoreSubscriptionInfo

- `billingPeriod` 和 `billingPeriodUnit`：续费周期；
- `hasTrialPeriod`：是否有试用；
- `trialPeriod` 和 `trialPeriodUnit`：试用周期；
- `StoreDurationUnit`：`minute`、`hour`、`day`、`week`、`month`、`year` 或 `unknown`。

### StorePrice

提供 `currencyCode`、格式化/数值基础价、当前价、续订价、`isOnSale` 和 `saleEndsAt`。界面展示应优先使用 `formattedPrice` 或 `formattedRecurrencePrice`，避免自行拼接币种格式。

### StoreAvailability

包含 Availability Store ID、结束时间、对应价格和原始扩展 JSON。

### StoreCollectionData

包含获得时间、开始/结束时间、试用状态、剩余试用时长、campaign ID、developer offer ID 和扩展 JSON。

### StoreLicense

| 字段/方法 | 说明 |
| --- | --- |
| `productStoreId` | Product Store ID |
| `skuStoreId` | SKU Store ID |
| `inAppOfferToken` | Developer offer token |
| `expirationDateEpochMilliseconds` | UTC Unix 到期时间戳 |
| `expiresAt` | 转换后的 UTC `DateTime` |
| `isEntitled` | 原生端根据许可证存在性与到期时间计算的结果 |
| `isCurrentlyEntitled({DateTime? at})` | 在指定时间或当前时间进行客户端权益判断 |
| `isActive` | 仅为源码兼容保留，已废弃，禁止用于 Add-on 权益判断 |
| `extendedJsonData` | Microsoft Store 原始扩展 JSON |

### StoreAppLicense

包含应用的 `isActive`、`isTrial`、`skuStoreId`、`expiresAt`、`trialTimeRemaining`、`trialUniqueId`、`extendedJsonData` 和 `addOnLicenses`。

## 错误处理

```dart
try {
  final status = await store.makePurchase(productStoreId);
  // 根据 status 更新 UI；成功后重新读取许可证。
} on PlatformException catch (error, stackTrace) {
  // 将 error.code、error.message、error.details 和 stackTrace 发送到日志系统。
}
```

原生 Microsoft Store 调用失败时会通过 `PlatformException` 返回；通道结构不符合预期时可能抛出 `FormatException`；空购买 ID 会抛出 `ArgumentError`。

## 商业服务端边界

不要把 Entra ID client secret、Microsoft Store 服务端访问令牌或 User Store ID key 放进此包或 Flutter 客户端。公开发布到 pub.dev 后，任何人都能读取包源码和应用二进制中的秘密。

建议业务流程：客户端购买成功后把业务用户 ID 和必要的 Store 标识发送给你的 HTTPS 服务端；服务端安全保存 Microsoft 凭据，调用 Microsoft Store Collections/Recurrence API；同时检查订阅状态和到期时间；把最终会员结果写入你自己的权益表，再向客户端返回短时有效的业务会话结果。

服务端通常还应保存：Product/SKU ID、用户与订阅的关联、订阅状态、到期时间、自动续费状态（接口提供时）、最后校验时间、最近错误以及退款/撤销结果。具体字段以你采用的 Microsoft Store 服务端 API 版本和响应为准。

## 迁移自 windows_iap

将依赖和导入改为：

```yaml
dependencies:
  windows_store_iap: ^0.1.0
```

```dart
import 'package:windows_store_iap/windows_store_iap.dart';
```

可以继续使用 `WindowsIap()`；也可以改为 `WindowsStoreIap()`。旧版 `StoreLicense.isActive` 不再代表订阅权益，请改用 `isCurrentlyEntitled()` 或 `checkPurchase()`。
