## 0.2.5

- Expand native regression coverage for synchronous Store failures, purchase
  retries, every purchase status, inactive/expired/unrelated licenses, wrong-thread
  calls, and recovery after a query timeout.
- Runtime behavior and public APIs are unchanged from 0.2.4.

## 0.2.4

- Return a structured error for missing native SKU/availability prices and
  collections instead of dereferencing a null WinRT interface.
- Add native catalog serialization tests covering nested prices, subscriptions,
  collection data, missing values, valid empty lists, and query HRESULTs.

## 0.2.3

- Track pending native Store requests and cancel them when the plugin shuts down.
- Guard retained MethodChannel handlers against accessing a destroyed plugin.
- Bound product and license queries to 30 seconds, return `store_timeout`, and
  discard late or duplicate replies after cancellation.
- Keep purchase UI requests pending until the Store resolves them; query
  deadlines do not release the concurrent-purchase guard.
- Return a structured error for an invalid/null Store async operation.
- Add STA and MTA native regression tests for shutdown, query cancellation,
  timeout/completion races, delayed purchases, and initialization failures.

## 0.2.2

- Fix a shutdown-time access violation caused by unregistering the MethodChannel
  after Flutter had started destroying the native plugin registrar.
- Add a native regression test that prevents plugin destruction from touching
  the messenger.

## 0.2.1

- Fix a native use-after-lifetime in the suspended entitlement check.
- Bind each StoreContext to the current plugin instance and validated window.
- Return purchase status consistently and add detailed HRESULT diagnostics.
- Reject overlapping purchase UI requests.
- Add release PDB generation and native regression tests.
- Harden the example runner, error handling, logging, and package metadata.

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
