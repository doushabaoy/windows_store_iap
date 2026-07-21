import '../src/utils.dart';

/// Localized and numeric Microsoft Store price information.
class StorePrice {
  /// ISO 4217 currency code.
  final String? currencyCode;

  /// Localized undiscounted price.
  final String? formattedBasePrice;

  /// Localized current purchase price.
  final String? formattedPrice;

  /// Localized recurring subscription price.
  final String? formattedRecurrencePrice;

  /// Numeric undiscounted price.
  final double? unformattedBasePrice;

  /// Numeric current purchase price.
  final double? unformattedPrice;

  /// Numeric recurring subscription price.
  final double? unformattedRecurrencePrice;

  /// Whether the product is currently on sale.
  final bool? isOnSale;

  /// UTC sale end time, when the price is promotional.
  final DateTime? saleEndsAt;

  /// Creates a Store price value.
  const StorePrice({
    this.currencyCode,
    this.formattedBasePrice,
    this.formattedPrice,
    this.formattedRecurrencePrice,
    this.unformattedBasePrice,
    this.unformattedPrice,
    this.unformattedRecurrencePrice,
    this.isOnSale,
    this.saleEndsAt,
  });

  /// Creates price information from a structured channel payload.
  factory StorePrice.fromJson(Map<String, dynamic> json) => StorePrice(
        currencyCode: json['currencyCode'] as String?,
        formattedBasePrice: json['formattedBasePrice'] as String?,
        formattedPrice: json['formattedPrice'] as String?,
        formattedRecurrencePrice: json['formattedRecurrencePrice'] as String?,
        unformattedBasePrice: asDouble(json['unformattedBasePrice']),
        unformattedPrice: asDouble(json['unformattedPrice']),
        unformattedRecurrencePrice:
            asDouble(json['unformattedRecurrencePrice']),
        isOnSale: json['isOnSale'] as bool?,
        saleEndsAt: utcDateTimeFromEpochMilliseconds(
          json['saleEndDateEpochMilliseconds'],
        ),
      );

  /// Converts this price to a JSON-compatible map.
  Map<String, dynamic> toJson() => {
        'currencyCode': currencyCode,
        'formattedBasePrice': formattedBasePrice,
        'formattedPrice': formattedPrice,
        'formattedRecurrencePrice': formattedRecurrencePrice,
        'unformattedBasePrice': unformattedBasePrice,
        'unformattedPrice': unformattedPrice,
        'unformattedRecurrencePrice': unformattedRecurrencePrice,
        'isOnSale': isOnSale,
        'saleEndDateEpochMilliseconds': saleEndsAt?.millisecondsSinceEpoch,
      };
}
