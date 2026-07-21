/// Microsoft Store purchases and subscription entitlements for Flutter on
/// Windows.
library windows_store_iap;

import 'windows_iap.dart';

export 'windows_iap.dart';

/// Recommended entry point for the `windows_store_iap` package.
///
/// It inherits the complete API from [WindowsIap]. The original class remains
/// available so applications migrating from `windows_iap` can update imports
/// without rewriting all call sites at once.
class WindowsStoreIap extends WindowsIap {}
