import '../src/utils.dart';
import 'store_license.dart';

/// License information for the current Store application and its add-ons.
class StoreAppLicense {
  /// Whether the application license is active.
  final bool isActive;

  /// Whether the application is currently using a trial license.
  final bool isTrial;

  /// SKU Store ID associated with the application license.
  final String? skuStoreId;

  /// UTC application-license expiration time, when present.
  final DateTime? expiresAt;

  /// Remaining trial duration, when the application is in a trial.
  final Duration? trialTimeRemaining;

  /// Microsoft Store identifier for the current trial.
  final String? trialUniqueId;

  /// Raw extended JSON supplied by Microsoft Store.
  final String? extendedJsonData;

  /// Add-on licenses keyed as returned by the Windows Store API.
  final Map<String, StoreLicense> addOnLicenses;

  /// Creates an application license value.
  const StoreAppLicense({
    required this.isActive,
    required this.isTrial,
    this.skuStoreId,
    this.expiresAt,
    this.trialTimeRemaining,
    this.trialUniqueId,
    this.extendedJsonData,
    this.addOnLicenses = const {},
  });

  /// Creates an application license from a structured channel payload.
  factory StoreAppLicense.fromJson(Map<String, dynamic> json) {
    final licenses = json['addOnLicenses'] is Map
        ? asStringKeyedMap(json['addOnLicenses'])
        : const <String, dynamic>{};
    final remaining = asInt(json['trialTimeRemainingMilliseconds']);
    return StoreAppLicense(
      isActive: json['isActive'] as bool? ?? false,
      isTrial: json['isTrial'] as bool? ?? false,
      skuStoreId: json['skuStoreId'] as String?,
      expiresAt: utcDateTimeFromEpochMilliseconds(
        json['expirationDateEpochMilliseconds'],
      ),
      trialTimeRemaining:
          remaining == null ? null : Duration(milliseconds: remaining),
      trialUniqueId: json['trialUniqueId'] as String?,
      extendedJsonData: json['extendedJsonData'] as String?,
      addOnLicenses: licenses.map(
        (key, value) => MapEntry(
          key,
          StoreLicense.fromJson(asStringKeyedMap(value)),
        ),
      ),
    );
  }

  /// Converts this license to a JSON-compatible map.
  Map<String, dynamic> toJson() => {
        'isActive': isActive,
        'isTrial': isTrial,
        'skuStoreId': skuStoreId,
        'expirationDateEpochMilliseconds': expiresAt?.millisecondsSinceEpoch,
        'trialTimeRemainingMilliseconds': trialTimeRemaining?.inMilliseconds,
        'trialUniqueId': trialUniqueId,
        'extendedJsonData': extendedJsonData,
        'addOnLicenses': addOnLicenses.map(
          (key, value) => MapEntry(key, value.toJson()),
        ),
      };
}
