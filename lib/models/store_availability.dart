import '../src/utils.dart';
import 'store_price.dart';

/// A purchasable availability for a Microsoft Store SKU.
class StoreAvailability {
  /// Availability Store ID used for purchase selection.
  final String? storeId;

  /// UTC time at which this availability ends, when defined.
  final DateTime? endsAt;

  /// Price for this availability.
  final StorePrice? price;

  /// Raw extended JSON supplied by Microsoft Store.
  final String? extendedJsonData;

  /// Creates a Store availability value.
  const StoreAvailability({
    this.storeId,
    this.endsAt,
    this.price,
    this.extendedJsonData,
  });

  /// Creates an availability from a structured channel payload.
  factory StoreAvailability.fromJson(Map<String, dynamic> json) =>
      StoreAvailability(
        storeId: json['storeId'] as String?,
        endsAt: utcDateTimeFromEpochMilliseconds(
          json['endDateEpochMilliseconds'],
        ),
        price: json['price'] is Map
            ? StorePrice.fromJson(asStringKeyedMap(json['price']))
            : null,
        extendedJsonData: json['extendedJsonData'] as String?,
      );

  /// Converts this availability to a JSON-compatible map.
  Map<String, dynamic> toJson() => {
        'storeId': storeId,
        'endDateEpochMilliseconds': endsAt?.millisecondsSinceEpoch,
        'price': price?.toJson(),
        'extendedJsonData': extendedJsonData,
      };
}
