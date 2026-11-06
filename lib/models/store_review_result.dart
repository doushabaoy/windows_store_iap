/// Outcome reported by Microsoft's in-app rating and review dialog.
enum StoreReviewStatus {
  /// Microsoft Store reported that this operation succeeded.
  succeeded,

  /// The user dismissed the dialog without submitting a review.
  canceledByUser,

  /// Microsoft Store reported a network error.
  networkError,

  /// Microsoft Store reported another error.
  error,
}

/// Result of one review request, not a record of the user's review history.
class StoreReviewResult {
  /// Status reported by Microsoft Store.
  final StoreReviewStatus status;

  /// Whether this submission updated an existing review.
  ///
  /// Read from `ExtendedJsonData.data.updated` only for successful responses.
  /// `false` means a new review; `null` means the information is unavailable.
  /// This is distinct from the native `WasUpdated` property.
  final bool? updatedExistingReview;

  /// Optional HRESULT formatted as `0x80072EE7`.
  final String? extendedErrorCode;

  /// Unmodified JSON returned by Microsoft Store, possibly empty or malformed.
  final String? extendedJsonData;

  /// Creates the result of a single review operation.
  const StoreReviewResult({
    required this.status,
    this.updatedExistingReview,
    this.extendedErrorCode,
    this.extendedJsonData,
  });

  /// Decodes the plugin's structured native response.
  factory StoreReviewResult.fromJson(Map<String, dynamic> json) {
    final status = json['status'];
    final updated = json['updatedExistingReview'];
    final error = json['extendedErrorCode'];
    final extendedJson = json['extendedJsonData'];
    if (status is! int ||
        status < 0 ||
        status >= StoreReviewStatus.values.length ||
        (updated != null && updated is! bool) ||
        (error != null && error is! String) ||
        (extendedJson != null && extendedJson is! String)) {
      throw const FormatException(
        'Microsoft Store returned an invalid review payload.',
      );
    }
    return StoreReviewResult(
      status: StoreReviewStatus.values[status],
      updatedExistingReview:
          status == StoreReviewStatus.succeeded.index ? updated as bool? : null,
      extendedErrorCode: error as String?,
      extendedJsonData: extendedJson as String?,
    );
  }
}
