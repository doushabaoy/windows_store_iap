# windows_store_iap example：真实商店验收

本示例保留内购与订阅按钮，新增独立的「评价应用」按钮。执行期间所有按钮禁用并
显示加载状态；界面保留返回状态、`updatedExistingReview`、HRESULT、原始 JSON，
或 `PlatformException` 的 code/message/details，不把完整响应写入日志。

## 准备 MSIX

1. 准备 Windows 10 1809（17763）以上或 Windows 11 真机，安装 Microsoft Store，
   登录用于测试的微软账号。不要以管理员身份运行应用。
2. 在 Partner Center 使用已关联的应用；商店提交、包可见性和测试账号权限均需有效。
   使用你现有项目的 MSIX 打包流程，将示例或新 API 接入该关联应用。
3. 打包时使用该应用自己的 Identity Name、Publisher、PublisherDisplayName 和签名，
   确保与 Partner Center 匹配；本仓库不提供、也不修改你的身份或证书。
4. 先从 Store 安装/获取该应用建立许可证上下文，再安装或更新为包含本地插件的测试
   MSIX。若使用私有分发/测试航班，确认测试账号有访问权限和许可证。
5. 示例通过父目录 path 依赖使用当前源码；业务应用可使用 path 依赖测试本地源码，
   或使用发布包 `windows_store_iap: ^0.3.0`。执行 `flutter pub get`、`flutter build windows --release`，
   再按现有流程打包 MSIX，保持包身份一致并递增应用包版本。
6. 普通用户权限启动安装后的应用，不以裸 exe / `flutter run` 的结果代替 MSIX 验收。

原生构建采用 Windows SDK 10.0.26100.0、C++/WinRT 2.0.221121.5；最低需要
Windows SDK 10.0.17763.0，Flutter 最低版本保持 3.7.0。

## 验收清单

下列均需实际人工执行，目前状态全部为 **未验证**。自动化测试只验证本地逻辑，
不代表真实微软商店评分窗口已经验证成功，也不会在测试中提交任何真实评价。

| 场景 | 步骤及期望结果 | 当前状态 |
| --- | --- | --- |
| 官方窗口 | 点击「评价应用」，出现微软官方评分/评论窗口，仍在应用上下文中 | 未验证 |
| 新评价 | 使用未评价此应用的测试账号正常提交；期望 succeeded，JSON data.updated 为 false 时显示 false | 未验证 |
| 修改评价 | 使用已有评价的同一账号修改并提交；期望 succeeded，JSON data.updated 为 true 时显示 true | 未验证 |
| 数据未知 | 若微软未提供有效 data.updated，界面显示 unknown，不根据 WasUpdated 猜测 | 未验证 |
| 用户取消 | 关闭/取消微软窗口，期望 canceledByUser，updatedExistingReview 为 unknown | 未验证 |
| 连续点击 | 快速点击按钮，单个弹窗；API 调用重叠时返回 review_in_progress | 未验证 |
| 长时间交互 | 让评分窗口停留超过 30 秒后操作，仍正确返回；期间不能启动第二个 Store 弹窗 | 未验证 |
| 评价和购买互斥 | 评价未结束时调用购买收到 review_in_progress；购买未结束时调用评价收到 purchase_in_progress | 未验证 |
| 关闭应用 | 评价请求未结束时关闭应用/销毁引擎，无崩溃，下一次启动能再次调用 | 未验证 |
| 网络与 Store 错误 | 断网或服务异常时观察 networkError/error 或原生 PlatformException，结束后可重试 | 未验证 |
| 管理员进程 | 管理员启动后微软 API 不支持；应返回错误，不能显示假成功 | 未验证 |
| 原内购和订阅 | 原按钮查询商品、许可证、checkPurchase 与购买流程正常，订阅结果和原版本一致 | 未验证 |
| 旧版系统 | 在 Windows 10 1809 上完成上述必要项目，核对系统兼容性 | 未验证 |

每次记录应用版本、插件版本、Windows build、普通/管理员权限、MSIX 身份、状态、
HRESULT 和 `updatedExistingReview`。不要把星级、评论正文或账号信息写入公开日志。
成功提交可能实际改变商店评价，请使用你有权测试的应用与账号。

## 自动化检查

在插件根目录运行 `flutter analyze`、`flutter test`，在本目录运行 `flutter test`
和 `flutter build windows --release`。原生测试命令在本目录执行：

```powershell
cmake -S windows -B build/windows/x64 -DWINDOWS_STORE_IAP_BUILD_TESTS=ON
cmake --build build/windows/x64 --config Release --target windows_store_iap_plugin_tests
ctest --test-dir build/windows/x64/plugins/windows_store_iap -C Release --output-on-failure
```

原生 Fake 隔离了 Store 服务，STA 和 MTA 两套测试验证状态、JSON、互斥、异常、
取消、窗口失效和销毁时的单次回传，同时保留全部商品/购买/订阅回归测试。

## 官方参考

- [RequestRateAndReviewAppAsync：系统版本、UI 线程、HWND 和管理员限制](https://learn.microsoft.com/en-us/uwp/api/windows.services.store.storecontext.requestrateandreviewappasync?view=winrt-26100)
- [StoreRateAndReviewResult：WasUpdated 与扩展数据](https://learn.microsoft.com/en-us/uwp/api/windows.services.store.storerateandreviewresult?view=winrt-26100)
- [评分响应 JSON 的 data.updated](https://learn.microsoft.com/en-us/windows/uwp/monetize/request-ratings-and-reviews)
