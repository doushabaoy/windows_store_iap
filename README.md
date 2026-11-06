# windows_store_iap

面向 Windows 桌面 Flutter 应用的 Microsoft Store 内购、订阅与应用内评价插件。

这是基于 `windows_iap` 的增强分支，重点修复订阅取消后仍长期被判定为会员的问题，并补充结构化许可证、SKU、价格和订阅数据。原项目版权与 BSD-3-Clause 许可证声明保留在 [LICENSE](LICENSE) 中；本项目与原作者不存在背书关系。

## 安装

```yaml
dependencies:
  windows_store_iap: ^0.3.0
```

```dart
import 'package:windows_store_iap/windows_store_iap.dart';
```

完整字段、返回值和异常说明见 [API 文档](doc/API.md)。

## 能力

- 购买 Durable、Consumable 和 UnmanagedConsumable 商品。
- 获取结构化 Product、SKU、Availability、Price、SubscriptionInfo 数据。
- 获取应用许可证、Add-on 许可证、试用信息和 UTC 到期时间。
- 调用微软官方应用内评分与评论窗口，返回本次操作状态和诊断信息。
- 使用 AddOnLicenses 存在性和 ExpirationDate 判断客户端订阅权益。
- MethodChannel 使用结构化数据，避免手工拼接 JSON 导致转义或解析错误。
- 提供 `requestReview`、`makePurchase`、`makePurchaseDetailed`、`getProducts`、`checkPurchase`、`getAddonLicenses`、`getAppLicense` API。
- 保留 `WindowsIap` 兼容类，并为新项目提供 `WindowsStoreIap` 入口。

## 异步请求与退出

商品和许可证查询（`getProducts`、`checkPurchase`、`getAddonLicenses`、
`getAppLicense`）最多等待 30 秒，超时抛出 `PlatformException`，其 `code`
为 `store_timeout`。原生端会请求取消该操作并忽略迟到结果；调用方可显示错误并重试。

购买界面不使用查询超时。原生购买尚未结束时，重复购买返回
`purchase_in_progress`，避免超时后再次发起付款。插件销毁时会收尾并请求取消未完成的
Store 操作，返回 `plugin_disposed`；系统取消属于尽力而为，不能据此认定付款已取消，
下次启动应重新查询许可证确认权益。

购买和评价共用弹窗互斥状态：当前为购买时，新的购买或评价抛出
`purchase_in_progress`；当前为评价时，新的购买或评价抛出
`review_in_progress`。评价窗口不应用 30 秒查询超时，结束或失败后释放互斥状态。

## 应用内评价

`WindowsStoreIap` 和兼容类 `WindowsIap` 均支持：

```dart
final store = WindowsStoreIap();
try {
  final result = await store.requestReview();
  switch (result.status) {
    case StoreReviewStatus.succeeded:
      // 如需保存已评价标记，由业务层自行持久化。
      debugPrint('评价成功；是否修改已有评价：${result.updatedExistingReview}');
      break;
    case StoreReviewStatus.canceledByUser:
      debugPrint('用户取消评价');
      break;
    case StoreReviewStatus.networkError:
      debugPrint('网络错误：${result.extendedErrorCode}');
      break;
    case StoreReviewStatus.error:
      debugPrint('评价失败：${result.extendedErrorCode}');
      break;
  }
} on PlatformException catch (error) {
  debugPrint('调用失败：${error.code}: ${error.message}');
}
```

以上代码需导入 `package:flutter/foundation.dart`、
`package:flutter/services.dart` 与本包入口。仅在用户主动触发的合适时机调用，
避免反复打扰用户。

| 字段 | 含义 |
| --- | --- |
| `status` | `succeeded`、`canceledByUser`、`networkError` 或 `error`，对应微软本次操作状态 |
| `updatedExistingReview` | `true` 修改已有评价，`false` 新评价，`null` 无法确定 |
| `extendedErrorCode` | 可选十六进制 HRESULT；`S_OK` 映射为 `null` |
| `extendedJsonData` | 微软返回的完整原始 JSON，可为空或格式无效；请勿默认记录或上传完整个人数据 |

`updatedExistingReview` 只有在原生 `Status == Succeeded`、JSON 的
`status == "success"` 且 `data.updated` 为布尔值时才有值。缺字段、空 JSON、
格式错误和非成功操作均为 `null`。官方 `WasUpdated` 是另一结果属性，原生通道
单独保留为 `wasUpdated`，不会用它猜测“新评价还是修改评价”。旧教程的
`OtherError` / `UpdatedExistingRatingOrReview` 不用于本实现。

此结果不包含星级、评论正文或可查询的历史评价记录。插件不会缓存“已评价”，
成功返回后是否保存本地标记由应用负责，该标记也不能代表服务器端评价历史。

### 评价环境与常见错误

- 运行系统：Windows 10 1809（Build 17763）及以上，包含 Windows 11。
- 构建需 Windows SDK 10.0.17763.0+，保持现有 CMake、C++/WinRT 和 Flutter
  最低版本；本次使用 SDK 10.0.26100.0 验证。
- 使用关联 Partner Center 应用身份的 MSIX，且测试账号需拥有该应用的 Store
  许可证；单独执行未打包的 `flutter run` 不足以验证评价窗口。
- 使用普通用户权限启动，微软明确不支持提升权限/管理员进程的应用内评价。
- `0x80070578`：无有效 Flutter HWND 或未正确绑定所有者窗口。
- `0x8001010E`：原生调用未在 Flutter 平台线程上启动。
- `0x80004001`：系统缺少 `IStoreContext4`，要求 Windows 10 1809+。
- 其他初始化/WinRT 异常保留 HRESULT 或 `native_error`，通过
  `PlatformException` 返回；微软返回的四种完成状态通过结果对象返回。
- `plugin_disposed`：引擎正在退出，原生取消为尽力而为，迟到回调会被忽略。
  不要把该错误当作用户取消评价，也不要立即另开 Store 弹窗。
- 无效通道数据会抛出 `FormatException`，不会伪装为评价成功。

### 测试与真实商店验收

Dart 与原生测试全部使用 Mock/Fake，不向微软提交评价。**单元测试通过只代表
本地逻辑正确，不代表真实微软商店评分窗口已经验证成功。**
真实窗口弹出、首次提交、修改旧评价、取消、快速点击和退出行为当前均为
**未验证**，也未在真实 Store 环境验证购买/订阅回归。

手动验收步骤和记录表见 [example 测试说明](example/README.md)。

## 原生回归测试

先在 `example` 下执行 `flutter pub get` 和 `flutter build windows --release`，
再执行：

```powershell
cmake -S windows -B build/windows/x64 -DWINDOWS_STORE_IAP_BUILD_TESTS=ON
cmake --build build/windows/x64 --config Release --target windows_store_iap_plugin_tests
ctest --test-dir build/windows/x64/plugins/windows_store_iap -C Release --output-on-failure
```

测试使用模拟 Store API，不发起购买、不显示应用窗口；同时覆盖 STA（桌面应用使用的
单线程单元）和 MTA，以及超时与完成同时发生时的回复互斥。
新增评价测试还覆盖 JSON 语义、错误、购买互斥、窗口无效、取消和插件销毁。

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
- [RequestRateAndReviewAppAsync](https://learn.microsoft.com/en-us/uwp/api/windows.services.store.storecontext.requestrateandreviewappasync?view=winrt-26100)
- [StoreRateAndReviewResult](https://learn.microsoft.com/en-us/uwp/api/windows.services.store.storerateandreviewresult?view=winrt-26100)
- [Request ratings and reviews](https://learn.microsoft.com/en-us/windows/uwp/monetize/request-ratings-and-reviews)
