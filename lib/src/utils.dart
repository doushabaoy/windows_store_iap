List<T> parseListNotNull<T extends Object?>({
  required List<dynamic> json,
  required T Function(Map<String, dynamic> json) fromJson,
}) {
  return json.map((e) => fromJson(asStringKeyedMap(e))).toList();
}

Map<String, dynamic> asStringKeyedMap(dynamic value) {
  if (value is! Map) {
    throw FormatException('Expected a map but received ${value.runtimeType}.');
  }
  return value.map((key, item) => MapEntry(key.toString(), item));
}

int? asInt(dynamic value) {
  if (value is int) return value;
  if (value is num) return value.toInt();
  return int.tryParse(value?.toString() ?? '');
}

double? asDouble(dynamic value) {
  if (value is double) return value;
  if (value is num) return value.toDouble();
  return double.tryParse(value?.toString() ?? '');
}

DateTime? utcDateTimeFromEpochMilliseconds(dynamic value) {
  final milliseconds = asInt(value);
  if (milliseconds == null) return null;
  return DateTime.fromMillisecondsSinceEpoch(milliseconds, isUtc: true);
}
