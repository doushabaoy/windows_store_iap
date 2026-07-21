import '../src/utils.dart';

/// Ownership and trial data for a SKU in the user's Store collection.
class StoreCollectionData {
  /// UTC acquisition time.
  final DateTime? acquiredAt;

  /// UTC entitlement start time.
  final DateTime? startsAt;

  /// UTC entitlement end time.
  final DateTime? endsAt;

  /// Whether this collection entry represents a trial.
  final bool? isTrial;

  /// Remaining trial duration, when available.
  final Duration? trialTimeRemaining;

  /// Campaign identifier associated with the acquisition.
  final String? campaignId;

  /// Developer offer identifier associated with the acquisition.
  final String? developerOfferId;

  /// Raw extended JSON supplied by Microsoft Store.
  final String? extendedJsonData;

  /// Creates a Store collection value.
  const StoreCollectionData({
    this.acquiredAt,
    this.startsAt,
    this.endsAt,
    this.isTrial,
    this.trialTimeRemaining,
    this.campaignId,
    this.developerOfferId,
    this.extendedJsonData,
  });

  /// Creates collection data from a structured channel payload.
  factory StoreCollectionData.fromJson(Map<String, dynamic> json) {
    final trialMilliseconds = asInt(json['trialTimeRemainingMilliseconds']);
    return StoreCollectionData(
      acquiredAt: utcDateTimeFromEpochMilliseconds(
        json['acquiredDateEpochMilliseconds'],
      ),
      startsAt:
          utcDateTimeFromEpochMilliseconds(json['startDateEpochMilliseconds']),
      endsAt:
          utcDateTimeFromEpochMilliseconds(json['endDateEpochMilliseconds']),
      isTrial: json['isTrial'] as bool?,
      trialTimeRemaining: trialMilliseconds == null
          ? null
          : Duration(milliseconds: trialMilliseconds),
      campaignId: json['campaignId'] as String?,
      developerOfferId: json['developerOfferId'] as String?,
      extendedJsonData: json['extendedJsonData'] as String?,
    );
  }

  /// Converts this collection data to a JSON-compatible map.
  Map<String, dynamic> toJson() => {
        'acquiredDateEpochMilliseconds': acquiredAt?.millisecondsSinceEpoch,
        'startDateEpochMilliseconds': startsAt?.millisecondsSinceEpoch,
        'endDateEpochMilliseconds': endsAt?.millisecondsSinceEpoch,
        'isTrial': isTrial,
        'trialTimeRemainingMilliseconds': trialTimeRemaining?.inMilliseconds,
        'campaignId': campaignId,
        'developerOfferId': developerOfferId,
        'extendedJsonData': extendedJsonData,
      };
}
