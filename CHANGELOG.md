## 0.2.0
- Initial release fo windwos_store_iap 0.2.0
- Fix the bug.

## 0.1.0

- Publish the enhanced fork as `windows_store_iap` and retain a compatibility
  entry point for existing `WindowsIap` users.
- Add complete public API documentation and a Chinese API reference.
- Correct subscription entitlement checks; `StoreLicense.IsActive` is no longer treated as an entitlement signal.
- Bound cached add-on licenses by their UTC expiration date.
- Correct WinRT-to-Unix time conversion and expose epoch milliseconds.
- Add structured app license, SKU, subscription, collection, price and availability models.
- Replace hand-built native JSON with typed Flutter MethodChannel values.
- Add argument validation, HRESULT propagation and native exception handling.
- Add model and MethodChannel regression tests.

## 0.0.1

- Initial release.
