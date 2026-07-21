import '../src/utils.dart';

/// Unit used for subscription billing and trial periods.
enum StoreDurationUnit {
  /// Minutes.
  minute,

  /// Hours.
  hour,

  /// Days.
  day,

  /// Weeks.
  week,

  /// Months.
  month,

  /// Years.
  year,

  /// A value not recognized by this package version.
  unknown,
}

StoreDurationUnit _durationUnit(dynamic value) {
  final index = asInt(value);
  if (index != null && index >= 0 && index <= 5) {
    return StoreDurationUnit.values[index];
  }
  return StoreDurationUnit.unknown;
}

/// Billing cadence and optional trial period for a subscription SKU.
class StoreSubscriptionInfo {
  /// Number of [billingPeriodUnit] units in each billing period.
  final int? billingPeriod;

  /// Unit used by [billingPeriod].
  final StoreDurationUnit billingPeriodUnit;

  /// Whether the subscription has a trial period.
  final bool? hasTrialPeriod;

  /// Number of [trialPeriodUnit] units in the trial.
  final int? trialPeriod;

  /// Unit used by [trialPeriod].
  final StoreDurationUnit trialPeriodUnit;

  /// Creates subscription billing information.
  const StoreSubscriptionInfo({
    this.billingPeriod,
    this.billingPeriodUnit = StoreDurationUnit.unknown,
    this.hasTrialPeriod,
    this.trialPeriod,
    this.trialPeriodUnit = StoreDurationUnit.unknown,
  });

  /// Creates subscription information from a structured channel payload.
  factory StoreSubscriptionInfo.fromJson(Map<String, dynamic> json) =>
      StoreSubscriptionInfo(
        billingPeriod: asInt(json['billingPeriod']),
        billingPeriodUnit: _durationUnit(json['billingPeriodUnit']),
        hasTrialPeriod: json['hasTrialPeriod'] as bool?,
        trialPeriod: asInt(json['trialPeriod']),
        trialPeriodUnit: _durationUnit(json['trialPeriodUnit']),
      );

  /// Converts this information to a JSON-compatible map.
  Map<String, dynamic> toJson() => {
        'billingPeriod': billingPeriod,
        'billingPeriodUnit': billingPeriodUnit.index,
        'hasTrialPeriod': hasTrialPeriod,
        'trialPeriod': trialPeriod,
        'trialPeriodUnit': trialPeriodUnit.index,
      };
}
