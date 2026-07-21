import '../src/utils.dart';
import 'store_availability.dart';
import 'store_collection_data.dart';
import 'store_price.dart';
import 'store_subscription_info.dart';

/// A purchasable Microsoft Store SKU.
class StoreSku {
  /// Microsoft Store SKU Store ID.
  final String? storeId;

  /// Localized SKU title.
  final String? title;

  /// Localized SKU description.
  final String? description;

  /// Language used by the localized metadata.
  final String? language;

  /// Whether this SKU is in the current user's collection.
  final bool? isInUserCollection;

  /// Whether this SKU is a recurring subscription.
  final bool? isSubscription;

  /// Whether this SKU represents a trial offer.
  final bool? isTrial;

  /// Developer-defined SKU metadata.
  final String? customDeveloperData;

  /// Current price information.
  final StorePrice? price;

  /// Billing and trial periods for subscription SKUs.
  final StoreSubscriptionInfo? subscriptionInfo;

  /// Current user's collection data for this SKU.
  final StoreCollectionData? collectionData;

  /// Purchasable availabilities for this SKU.
  final List<StoreAvailability> availabilities;

  /// Raw extended JSON supplied by Microsoft Store.
  final String? extendedJsonData;

  /// Creates a Store SKU value.
  const StoreSku({
    this.storeId,
    this.title,
    this.description,
    this.language,
    this.isInUserCollection,
    this.isSubscription,
    this.isTrial,
    this.customDeveloperData,
    this.price,
    this.subscriptionInfo,
    this.collectionData,
    this.availabilities = const [],
    this.extendedJsonData,
  });

  /// Creates a SKU from a structured channel payload.
  factory StoreSku.fromJson(Map<String, dynamic> json) => StoreSku(
        storeId: json['storeId'] as String?,
        title: json['title'] as String?,
        description: json['description'] as String?,
        language: json['language'] as String?,
        isInUserCollection: json['isInUserCollection'] as bool?,
        isSubscription: json['isSubscription'] as bool?,
        isTrial: json['isTrial'] as bool?,
        customDeveloperData: json['customDeveloperData'] as String?,
        price: json['price'] is Map
            ? StorePrice.fromJson(asStringKeyedMap(json['price']))
            : null,
        subscriptionInfo: json['subscriptionInfo'] is Map
            ? StoreSubscriptionInfo.fromJson(
                asStringKeyedMap(json['subscriptionInfo']),
              )
            : null,
        collectionData: json['collectionData'] is Map
            ? StoreCollectionData.fromJson(
                asStringKeyedMap(json['collectionData']),
              )
            : null,
        availabilities: (json['availabilities'] as List? ?? const [])
            .map((item) => StoreAvailability.fromJson(asStringKeyedMap(item)))
            .toList(growable: false),
        extendedJsonData: json['extendedJsonData'] as String?,
      );

  /// Converts this SKU to a JSON-compatible map.
  Map<String, dynamic> toJson() => {
        'storeId': storeId,
        'title': title,
        'description': description,
        'language': language,
        'isInUserCollection': isInUserCollection,
        'isSubscription': isSubscription,
        'isTrial': isTrial,
        'customDeveloperData': customDeveloperData,
        'price': price?.toJson(),
        'subscriptionInfo': subscriptionInfo?.toJson(),
        'collectionData': collectionData?.toJson(),
        'availabilities': availabilities.map((item) => item.toJson()).toList(),
        'extendedJsonData': extendedJsonData,
      };
}
