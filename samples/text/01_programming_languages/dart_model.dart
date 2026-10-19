class DocumentSummary {
  final String id;
  final String title;
  final int pageCount;
  final DateTime lastViewed;

  const DocumentSummary({
    required this.id,
    required this.title,
    required this.pageCount,
    required this.lastViewed,
  });

  Map<String, dynamic> toJson() => {
        'id': id,
        'title': title,
        'pageCount': pageCount,
        'lastViewed': lastViewed.toIso8601String(),
      };
}
