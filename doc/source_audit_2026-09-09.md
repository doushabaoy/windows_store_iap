# windows_store_iap 0.2.0 源码审查

审查日期：2026-09-09。原始源码基线：`8c3aa5d`。随后按审查结果完成
0.2.1 修复；本文后面的发现保留原始代码位置和论证，供追踪问题来源。

原始结论：发现两个具有明确触发条件的原生内存安全缺陷，以及窗口归属、
购买结果契约、发布符号和测试覆盖方面的问题。仅凭
`ACCESS_VIOLATION_c0000005_windows_store_iap_plugin.dll!Unknown`，尚不能把
某一次线上崩溃归因到具体行。

## 0.2.1 修复后复核

| 项目 | 修复结果 |
| --- | --- |
| F1 协程悬空 Store ID | `CheckPurchase` 现在按值持有 `std::string`；延迟 75ms 恢复的原生测试确认调用帧销毁后仍能正确匹配订阅。 |
| F2 空 View | 注册过程不再解引用 View；调用时重新取得并验证 HWND。无 View 测试返回 `0x80070578` 且不创建 StoreContext。 |
| F3 全局窗口 | 删除 `g_root_window`；每个插件实例保存自己的 View 获取器，每次请求解析当前根窗口。双实例测试确认两个 Store 工厂收到各自 HWND。 |
| F4 状态契约 | `makePurchase` 始终返回 Store 状态；新增 `makePurchaseDetailed` 同时返回状态和十六进制 HRESULT。 |
| F5 发布符号 | Release/Profile 启用 `/Zi` 与 `/DEBUG:FULL`。Release 构建生成与插件 DLL 同次构建的 PDB。 |
| F6 原生测试 | 新增 5 个 C++ 回归场景，并补充 2 个 Dart 购买结果测试和有效的示例 Widget 测试。 |
| F7 Runner 空指针 | `WM_FONTCHANGE` 同时检查 controller 与 engine。 |
| 其他低优先级项 | 修正 UTF-16 转换尾零、User32 句柄释放、示例异常与重复提交、日志最小化、示例打包残留、版本文档和无效脚本。 |

最终复核：CodeGraph 39 个文件、403 个节点、868 条关系，索引为最新；
`CheckPurchase` 的协程签名为按值参数。Flutter analyze 零诊断，12 个包测试、
1 个示例 Widget 测试、5 个原生测试全部通过，Windows Debug/Release 构建通过。
本地未启动示例应用，也未发起真实 Microsoft Store 购买；真实商店账号、包关联和
线上旧 DLL 的转储仍需在对应环境验证。

## 阅读范围与方法

已逐文件阅读全部 51 个已跟踪文本文件，共 3,462 行；另一个已跟踪文件是图标二进制，不属于源码。覆盖范围如下，统计不包含本报告：

| 范围 | 文本文件数 | 行数 |
| --- | ---: | ---: |
| `lib/`：入口、平台接口、通道、8 个模型、工具函数 | 13 | 952 |
| `test/` | 2 | 191 |
| `windows/`：插件实现、头文件、C 入口、CMake | 5 | 599 |
| `example/lib/` | 2 | 161 |
| `example/test/` | 1 | 16 |
| `example/windows/`：runner、注册文件、资源脚本和构建配置 | 16 | 1,022 |
| `doc/API.md` | 1 | 176 |
| 根目录及示例的其余文档、包配置、忽略文件、脚本 | 11 | 345 |

按要求使用本机 CodeGraph 1.3.1，实际执行了 `init`、`sync`、`status`、`files`、`node --file`、`node <symbol>`、`callers`、`callees`、`impact`、`affected`。

索引结果为 36 个文件、294 个节点、650 条关系。CMake、资源文件及部分文档不在此索引范围，另行全文阅读。MethodChannel 的 Dart/C++ 边界通过五个方法名、参数和返回字段人工核对，未把静态图当作跨语言调用的完整证明。

此外读取了本地 Flutter messenger、MethodChannel、registrar 源码，以及本项目生成的 C++/WinRT 2.0.221121.5 头文件，用于核实引用计数、回调线程和协程行为。

## 需要修复的问题

### F1 · P1：CheckPurchase 在协程恢复后使用悬空字符串引用

位置：[参数定义](D:/MyDesktop/packages/windows_store_iap-0.2.0/windows/windows_iap_plugin.cpp:365)、[等待后的读取](D:/MyDesktop/packages/windows_store_iap-0.2.0/windows/windows_iap_plugin.cpp:379)、[传入临时对象](D:/MyDesktop/packages/windows_store_iap-0.2.0/windows/windows_iap_plugin.cpp:433)。

`store_id.value_or("")` 返回新的临时 `std::string`，无论 optional 是否有值都会发生。`CheckPurchase` 用 `const std::string&` 接收它；在 `GetAppLicenseAsync()` 真正挂起、调用方继续执行后，临时字符串于调用语句结束时销毁。随后协程恢复，若应用许可证有效且 AddOnLicenses 非空，就会在 `store_id.empty()` 或 ID 比较中读取已失效对象。

这是未定义行为：可能闪退、匹配错误，也可能暂时表现正常。许可证为空、应用许可证无效或操作没有真正异步挂起时，可能绕过有问题的读取，因而不一定每次复现。

最小修复：将协程参数改成 `std::string store_id`，让协程帧拥有字符串。同步辅助函数 `LicenseMatchesStoreId` 的引用参数可以保留，不能机械地把所有引用全部修改。

已使用同一版本 C++/WinRT 的 `IAsyncAction`、MSVC C++17 `/await` 和手动完成点验证此生命周期模式。探针用包含字符串的追踪对象记录构造/析构，避免为了验证而解引用无效内存：

```text
按引用：before_suspend alive=true
        after_resume   alive=false
按值：  before_suspend alive=true
        after_resume   alive=true
```

这个实验验证参数生命周期，不是实际商店 DLL 崩溃重现，也不是线上转储的符号化结果。

微软明确说明：协程挂起后继续使用调用方持有的引用存在生命周期问题，协程参数默认应按值传递。[C++/WinRT 参数传递规则](https://learn.microsoft.com/en-us/windows/apps/develop/cpp-winrt/concurrency#parameter-passing)

### F2 · P1：注册插件时未检查 GetView() 是否为空

位置：[windows_iap_plugin.cpp:397](D:/MyDesktop/packages/windows_store_iap-0.2.0/windows/windows_iap_plugin.cpp:397)。

`registrar->GetView()->GetNativeWindow()` 直接解引用视图。Flutter 的 `GetView()` 合同允许在没有隐式视图时返回 `nullptr`。在这种引擎上注册插件，会在任何 Dart 内购调用之前发生原生访问冲突。

适用场景：无界面引擎、禁用隐式视图、宿主在建立视图之前注册插件。正常单窗口示例未必触发。扩展原生生命周期分析也在第 400 行报告了 `GetView` 的潜在空指针。

修复方向：注册过程允许视图缺失；需要购买界面时获取并验证对应视图和窗口，对没有可用窗口的请求返回明确错误。不能仅在注册时判空后让购买请求继续使用未初始化的窗口上下文。

依据：[Flutter PluginRegistrarWindows 源码](https://api.flutter.dev/windows-embedder/classflutter_1_1_plugin_registrar_windows.html)。

### F3 · P2：g_root_window 被全部插件实例共享，窗口归属和有效期不受管理

位置：[全局句柄及 GetStore](D:/MyDesktop/packages/windows_store_iap-0.2.0/windows/windows_iap_plugin.cpp:33)、[写入句柄](D:/MyDesktop/packages/windows_store_iap-0.2.0/windows/windows_iap_plugin.cpp:399)、[空析构函数](D:/MyDesktop/packages/windows_store_iap-0.2.0/windows/windows_iap_plugin.cpp:416)。

同进程先注册引擎 A、再注册引擎 B，B 就会覆盖 A 使用的窗口句柄。之后 A 的商店请求也会绑定到 B。B 被销毁后，全局变量仍保存这个非空句柄，A 的请求仍可能将它交给 `IInitializeWithWindow::Initialize`。

因此多引擎场景存在明确的窗口归属错误；如果不同平台线程同时注册/读取，也没有同步保护。无效 HWND 常表现为 HRESULT 或购买界面问题，不能据此断言一定产生 `c0000005`。

示例还在 `SetChildContent` 之前注册插件；此时获取的 `GA_ROOT` 未必已是最终宿主顶层窗口。应在视图完成挂接后或发起需要 UI 的操作时确认窗口归属。

修复方向：按插件实例保存上下文和窗口关联，建立窗口失效处理；在正确的窗口线程上初始化 StoreContext。单独加一次 `IsWindow` 不能解决跨实例归属问题。

### F4 · P2：ExtendedError 分支覆盖了公开的购买状态契约

位置：[MakePurchase](D:/MyDesktop/packages/windows_store_iap-0.2.0/windows/windows_iap_plugin.cpp:301)、[Dart 状态枚举](D:/MyDesktop/packages/windows_store_iap-0.2.0/lib/windows_iap.dart:25)、[现有状态测试](D:/MyDesktop/packages/windows_store_iap-0.2.0/test/windows_iap_method_channel_test.dart:18)。

如果 Store 返回 `NetworkError`、`ServerError`，或带扩展错误的 `NotPurchased`，原生代码会先进入 `ExtendedError != S_OK` 分支并发送错误，随后 `co_return`。Dart 收到的是 `PlatformException`，而不是公开枚举中相应的状态；原始 `Status` 也没有放进错误详情。

现有测试只模拟整数 0–4，验证了 Dart 枚举映射，完全绕过这一原生分支，所以无法暴露契约差异。

修复方向：统一契约。若保持当前枚举 API，应明确哪些完成状态返回枚举、哪些调用失败抛异常；同时保留 HRESULT 和原始状态用于诊断。若选择统一抛异常，则应同步修改文档和调用方，不能同时宣称一定返回全部状态。

此外 HRESULT 有时编码成十六进制字符串，有时编码成有符号十进制字符串，不利于调用方按错误码分类，建议统一。

微软示例把 ExtendedError 作为补充诊断信息，再按 Status 区分操作结果。[官方购买示例](https://learn.microsoft.com/en-us/windows/uwp/monetize/enable-in-app-purchases-of-apps-and-add-ons)

### F5 · P2：示例的 Release/Profile 构建未生成插件调试符号

位置：[插件继承宿主编译配置](D:/MyDesktop/packages/windows_store_iap-0.2.0/windows/CMakeLists.txt:58)、[示例编译配置](D:/MyDesktop/packages/windows_store_iap-0.2.0/example/windows/CMakeLists.txt:40)。

旧缓存和本次干净配置生成的插件 vcxproj 均显示：Debug 生成 ProgramDatabase，Release/Profile 的 DebugInformationFormat 为空，GenerateDebugInformation 为 false。插件 CMake 没有单独为 Release 指定符号生成。本次成功构建的 Release 插件目录包含 221,696 字节的 DLL 及 exp/lib，没有对应插件 PDB。

这解释了为什么使用此默认配置的发布版本可能只看到 `dll!Unknown`，但不能证明线上宿主工程使用的也是该配置。符号缺失影响定位能力，本身不制造访问冲突。

修复方向：发布时保留优化，同时为插件开启编译和链接符号，保存与每个发布 DLL 精确匹配的 PDB。重编出来的 PDB 不能代替旧发布版本的 PDB。

### F6 · P2：测试没有覆盖原生实现和协程挂起

位置：[通道测试](D:/MyDesktop/packages/windows_store_iap-0.2.0/test/windows_iap_method_channel_test.dart:9)、[空示例测试](D:/MyDesktop/packages/windows_store_iap-0.2.0/example/test/widget_test.dart:11)。

包内 10 个测试覆盖模型和模拟 MethodChannel，不加载原生 DLL。示例唯一的 widget test 没有构建 UI、没有断言，因此即使通过也不提供行为验证。CodeGraph 对 `windows/windows_iap_plugin.cpp` 的 `affected` 查询未找到测试；结合测试全文可确认没有原生回归覆盖，不能只依据静态图的空结果下结论。

建议补充真正有区分力的场景：许可证操作延迟完成后再读 ID、无视图注册、双引擎窗口归属、扩展错误与购买状态组合。Dart 模型再增加边界日期、未知枚举和旧 JSON 通道负载用例。

### F7 · P2：示例 WM_FONTCHANGE 分支可绕过 controller 判空

位置：[example/windows/runner/flutter_window.cpp:54](D:/MyDesktop/packages/windows_store_iap-0.2.0/example/windows/runner/flutter_window.cpp:54)。

函数开头的 `if (flutter_controller_)` 只保护了 Flutter 消息转发。后面的 `WM_FONTCHANGE` 分支仍无条件访问 `flutter_controller_->engine()`。如果窗口存在但 controller 尚未建立或已清空时收到此消息，会解引用空指针。

修复方向：在该分支中同时检查 controller 和 engine。该缺陷位于示例 runner，预期故障模块是宿主 EXE，不能把它当成插件 DLL 报错的已证实原因。

## 其他检查结果及低优先级问题

| 范围 | 结果 |
| --- | --- |
| Dart/C++ 通道契约 | 五个方法名和 `storeId` 键一致；产品、SKU、许可证、时间字段的传输类型与解析逻辑基本匹配。F4 的错误/状态契约需要统一。 |
| 日期换算 | WinRT 1601 年起点到 Unix 1970 年起点偏移量正确，100ns 到毫秒换算正确；现有日期测试通过。未发现由该转换导致原生访问冲突的证据。 |
| 价格解析 | C++ 的 UnformattedPrice 等返回字符串，Dart 用 `asDouble` 解析，是有意转换，不能误报为 native/Dart 类型不匹配。 |
| WinRT 对象判空 | ProductToMap/PriceToMap 等假设 API 返回的 product/price 非空。建议在外部数据边界防御性检查；未获得 Store 正常返回空 price 的可靠证据，因此未将其列为已确认崩溃原因。 |
| 许可证状态 | 到期时间限制和“不用 add-on IsActive 单独判权益”的实现与本项目文档一致；客户端时钟/缓存与服务端权益校验的边界已有说明。 |
| 示例异常处理 | 多个按钮直接 await 后没有 catch，Store 失败可能成为未处理的 Dart 异常；不能据此解释原生 c0000005。 |
| 示例并发与日志 | 按钮没有进行中状态，可重复发起购买；日志输出完整扩展许可证内容。需要精简日志并串行管理购买 UI。 |
| 示例打包 | `example/pubspec.yaml:96` 指向 `assets/icon.jpg`，仓库没有该文件；同时保留具体应用的 MSIX 标识。通用示例不能直接照此配置打包。本次未修改身份信息、未执行 MSIX 打包。 |
| 文档版本 | README 安装示例和 API 文档仍为 0.1.0，当前 pubspec 为 0.2.0。应同步版本说明。 |
| watch.bat | 调用 build_runner，但本包未声明该开发依赖，也没有代码生成配置；脚本无法按现有依赖独立工作。 |
| 窗口辅助函数 | `Utf8FromUtf16` 使用长度 -1 转换且没有移除终止 NUL，返回的 std::string 长度包含结尾 NUL；属于示例命令行参数处理问题。 |
| 资源释放 | EnableFullDpiSupportIfAvailable 仅在 GetProcAddress 成功时 FreeLibrary；符号不存在时会漏掉一次模块引用释放。对支持该 API 的系统不触发，不是本次 DLL 崩溃的主要方向。 |
| Consumable 支持范围 | 当前提供购买入口，没有消费余额/履行操作接口。若业务需要消耗型商品完整流程，尚需额外实现；对当前订阅审查不展开功能改造。 |

## 已核实、不能直接认定为崩溃原因的写法

1. **局部 MethodChannel。** Flutter 处理器由 messenger 持有，销毁 MethodChannel 不会自动注销 handler。本项目的局部 channel 并不意味着注册函数退出后回调立刻失效。
2. **协程按值持有 MethodResult。** unique_ptr 被移进协程帧；它不是指向调用方局部 unique_ptr 的引用。
3. **引擎销毁后的回复。** 当前本地 Flutter wrapper 的回复闭包持有 messenger 引用、加锁，并在引擎不可用时丢弃回复；不能仅凭插件没有取消表就认定回复必然 use-after-free。
4. **忽略 IAsyncAction 返回对象。** 当前 C++/WinRT promise 在协程完成前保留自身引用，忽略返回对象并不等于马上销毁运行中的协程。未观察失败状态会降低诊断能力，但不是 F1 的根因。
5. **MakePurchase 的 const hstring&。** 当前代码只在第一次挂起之前将 store_id 传给同步的 WinRT ABI 入口，恢复后没有再次读取。因此它与 CheckPurchase 不同；可以按值加固，但未证明当前实现有同样的挂起后悬空读取。
6. **GetProducts 的初始化列表。** 本项目 C++/WinRT 的 `param::async_iterable` 把 initializer_list 复制到拥有存储的 vector。扩展分析给出的临时对象告警未被独立证实，不能把它与 F1 混为一谈。
7. **恢复线程。** C++/WinRT 的 WinRT await adapter 保存/恢复 apartment 上下文；示例初始化 STA。未发现本插件主动切到任意后台线程后调用 Store UI 的代码。
8. **已有 try/catch。** `/EHsc` 下的 hresult_error/std::exception 捕获不会把 Windows 访问冲突自动变成 Dart PlatformException。应修复无效内存访问，不能靠追加 catch 来掩盖。[MSVC 异常模型](https://learn.microsoft.com/en-us/cpp/build/reference/eh-exception-handling-model?view=msvc-170)

## 验证记录

环境：Flutter 3.44.7 / Dart 3.12.2 / MSVC 14.44.35207。原生单文件分析使用 SDK 10.0.22621.0 和已有 C++/WinRT 2.0.221121.5 生成头文件。

| 检查 | 结果与限制 |
| --- | --- |
| CodeGraph init/sync/status | 成功，索引最新；`init` 检测到原有索引，随后执行 sync。 |
| Dart 格式 | `dart format --output=none --set-exit-if-changed lib test example/lib example/test`：18 个文件、0 改动。 |
| Flutter 静态分析 | 恢复依赖后无 error/warning；仍有 17 条 example/lib/app_logger.dart 的 avoid_print info，不是完全零诊断。 |
| 包单元测试 | 10/10 通过；全部为 Dart 层测试。 |
| 示例测试 | 1/1 通过，但该测试为空，无有效断言。 |
| MSVC 基础原生分析 | 完成，告警位于 SDK/WinRT 头文件；没有证明原生运行时安全。 |
| MSVC 扩展生命周期分析 | 完成；总计 983 条告警，其中 27 条定位到插件源文件。大量为外部库/智能指针模型噪声，已按调用上下文人工审查；第 400 行视图空指针与 F2 相符。未报出 F1 的 C26811，故不能依赖工具自动发现全部问题。 |
| WinRT 生命周期对照实验 | 按引用恢复时对象已销毁，按值恢复时存活；验证参数生命周期，不调用真实 Store。 |
| Windows Debug 干净构建 | 在临时源码副本中通过。未启动应用。 |
| Windows Release 干净构建 | 在临时源码副本中通过；插件 DLL 已生成，未生成对应插件 PDB。未启动应用。 |
| 实际商店购买、无视图引擎和双引擎运行 | 未执行；本次没有操作购买界面或启动用户应用。 |
| 线上崩溃转储 | 未提供，与线上二进制匹配的堆栈/符号尚未验证。 |

首次 `flutter analyze --no-pub` 因旧 package_config 与缺失缓存失败。离线恢复确认缺少 flutter_lints 2.0.1，随后正常 `flutter pub get` 下载已有依赖并适配当前 SDK：生成的锁文件中 meta 变为 1.18.0、test_api 变为 0.7.11。没有修改 pubspec.yaml 或主动升级依赖约束。

原项目示例构建缓存指向旧目录 `D:\MyDesktop\windows_iap-0.0.1`，因此完整构建使用临时副本，不在旧 CMake 缓存上操作。临时副本重新解析依赖，部分兼容版本可与原锁文件不同；故构建结果验证的是当前源码在上述工具链上可编译，不能替代线上发布构建的二进制复现。

两个构建均有 MSB8029 临时目录增量构建提示，未导致编译失败；本次使用干净副本完成验证，不将该提示归为插件源码缺陷。

构建与原生检查产物目录：`C:\Users\Mayn\AppData\Local\Temp\windows-store-iap-audit-20260909`。目录包含单文件分析日志、lifetime_trace.cpp 和按值/引用两组输出。ASan 尝试在 WinRT 初始化阶段报告非目标栈访问，未用作本插件故障证据。

## 建议的处理顺序

先修 F1 和 F2，补充能真正挂起协程的原生回归用例；再处理实例窗口归属与购买状态契约。发布构建同时保留对应 DLL/PDB，记录最近一次调用的方法名与阶段，用故障转储确认线上崩溃点。真实购买验证需要用户的商店关联应用环境；本报告不把未执行的场景标成通过。
