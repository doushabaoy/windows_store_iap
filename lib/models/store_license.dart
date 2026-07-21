import '../src/utils.dart';

/// A Microsoft Store add-on license.
class StoreLicense {
  /// Retained for source compatibility. Microsoft documents that add-on
  /// StoreLicense.IsActive currently always returns true.
  @Deprecated('Use isCurrentlyEntitled() instead.')
  final bool? isActive;

  /// Native entitlement result computed from presence and expiration time.
  final bool? isEntitled;

  /// SKU Store ID associated with this license.
  final String? skuStoreId;

  /// Product Store ID associated with this license.
  final String? productStoreId;

  /// Developer-defined in-app offer token.
  final String? inAppOfferToken;

  /// Raw Windows Runtime ticks since 1601-01-01 UTC.
  final num? expirationDate;

  /// License expiration as Unix epoch milliseconds in UTC.
  final int? expirationDateEpochMilliseconds;

  /// Raw extended JSON supplied by Microsoft Store.
  final String? extendedJsonData;

  /// Creates an add-on license value.
  const StoreLicense({
    this.isActive,
    this.isEntitled,
    this.skuStoreId,
    this.productStoreId,
    this.inAppOfferToken,
    this.expirationDate,
    this.expirationDateEpochMilliseconds,
    this.extendedJsonData,
  });

  /// UTC expiration time converted from the structured epoch value or legacy
  /// Windows Runtime ticks.
  DateTime? get expiresAt {
    final epochMilliseconds = expirationDateEpochMilliseconds;
    if (epochMilliseconds != null) {
      return DateTime.fromMillisecondsSinceEpoch(
        epochMilliseconds,
        isUtc: true,
      );
    }

    final ticks = asInt(expirationDate);
    if (ticks == null) return null;
    const unixEpochInWinRtTicks = 116444736000000000;
    const ticksPerMillisecond = 10000;
    return DateTime.fromMillisecondsSinceEpoch(
      (ticks - unixEpochInWinRtTicks) ~/ ticksPerMillisecond,
      isUtc: true,
    );
  }

  /// Backwards-compatible alias for [expiresAt].
  DateTime? getExpirationDate() => expiresAt;

  /// Client-side entitlement check that also bounds stale offline licenses by
  /// their Microsoft Store expiration time.
  bool isCurrentlyEntitled({DateTime? at}) {
    if (isEntitled == false) return false;
    final expiration = expiresAt;
    if (expiration == null) return isEntitled == true;
    return expiration.isAfter((at ?? DateTime.now()).toUtc());
  }

  /// Creates an add-on license from a structured channel payload.
  factory StoreLicense.fromJson(Map<String, dynamic> json) {
    return StoreLicense(
      isActive: json['isActive'] as bool?,
      isEntitled: json['isEntitled'] as bool?,
      skuStoreId: json['skuStoreId'] as String?,
      productStoreId: json['productStoreId'] as String?,
      inAppOfferToken: json['inAppOfferToken'] as String?,
      expirationDate: json['expirationDate'] as num?,
      expirationDateEpochMilliseconds:
          asInt(json['expirationDateEpochMilliseconds']),
      extendedJsonData: json['extendedJsonData'] as String?,
    );
  }

  /// Converts this license to a JSON-compatible map.
  Map<String, dynamic> toJson() => {
        'isActive': isActive,
        'isEntitled': isEntitled,
        'skuStoreId': skuStoreId,
        'productStoreId': productStoreId,
        'inAppOfferToken': inAppOfferToken,
        'expirationDate': expirationDate,
        'expirationDateEpochMilliseconds': expirationDateEpochMilliseconds,
        'extendedJsonData': extendedJsonData,
      };
}
