import '../src/utils.dart';
import 'store_price.dart';
import 'store_sku.dart';

/// A Microsoft Store product and its purchasable SKUs.
class Product {
  /// Localized product title.
  final String? title;

  /// Localized product description.
  final String? description;

  /// Backwards-compatible formatted default price.
  final String? price;

  /// Whether any SKU is in the current user's collection.
  final bool? inCollection;

  /// Microsoft Store product kind, such as `Durable` or `Consumable`.
  final String? productKind;

  /// Microsoft Store Product Store ID.
  final String? storeId;

  /// Developer-defined offer token, when configured.
  final String? inAppOfferToken;

  /// Language used by the localized Store metadata.
  final String? language;

  /// Product-level price information.
  final StorePrice? storePrice;

  /// SKUs offered for this product.
  final List<StoreSku> skus;

  /// Raw extended JSON supplied by Microsoft Store.
  final String? extendedJsonData;

  /// Creates a Store product value.
  const Product({
    required this.title,
    required this.description,
    required this.price,
    required this.inCollection,
    required this.productKind,
    required this.storeId,
    this.inAppOfferToken,
    this.language,
    this.storePrice,
    this.skus = const [],
    this.extendedJsonData,
  });

  /// A compact, backwards-compatible display string.
  String? get formattedTitle =>
      '$title ($productKind) $price, InUserCollection: $inCollection';

  /// Whether any SKU in this product is a subscription.
  bool get isSubscription => skus.any((sku) => sku.isSubscription == true);

  /// Creates a product from the plugin's structured channel payload.
  factory Product.fromJson(Map<String, dynamic> json) => Product(
        title: json['title'] as String?,
        description: json['description'] as String?,
        price: json['price'] as String?,
        inCollection: json['inCollection'] as bool?,
        productKind: json['productKind'] as String?,
        storeId: json['storeId'] as String?,
        inAppOfferToken: json['inAppOfferToken'] as String?,
        language: json['language'] as String?,
        storePrice: json['storePrice'] is Map
            ? StorePrice.fromJson(asStringKeyedMap(json['storePrice']))
            : null,
        skus: (json['skus'] as List? ?? const [])
            .map((item) => StoreSku.fromJson(asStringKeyedMap(item)))
            .toList(growable: false),
        extendedJsonData: json['extendedJsonData'] as String?,
      );

  /// Converts this product to a JSON-compatible map.
  Map<String, dynamic> toJson() => {
        'title': title,
        'description': description,
        'price': price,
        'inCollection': inCollection,
        'productKind': productKind,
        'storeId': storeId,
        'inAppOfferToken': inAppOfferToken,
        'language': language,
        'storePrice': storePrice?.toJson(),
        'skus': skus.map((item) => item.toJson()).toList(),
        'extendedJsonData': extendedJsonData,
      };
}
