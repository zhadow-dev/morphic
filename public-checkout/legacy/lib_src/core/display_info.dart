/// Monitor/display information from the native display manager.
class DisplayInfo {
  final bool isPrimary;
  final double dpiX;
  final double dpiY;
  final double scaleFactor;
  final int refreshRate;
  final int boundsLeft;
  final int boundsTop;
  final int boundsRight;
  final int boundsBottom;
  final int workLeft;
  final int workTop;
  final int workRight;
  final int workBottom;

  DisplayInfo({
    required this.isPrimary,
    required this.dpiX,
    required this.dpiY,
    required this.scaleFactor,
    required this.refreshRate,
    required this.boundsLeft,
    required this.boundsTop,
    required this.boundsRight,
    required this.boundsBottom,
    required this.workLeft,
    required this.workTop,
    required this.workRight,
    required this.workBottom,
  });

  factory DisplayInfo.fromMap(Map<Object?, Object?> map) {
    return DisplayInfo(
      isPrimary: map['isPrimary'] as bool? ?? false,
      dpiX: (map['dpiX'] as num?)?.toDouble() ?? 96.0,
      dpiY: (map['dpiY'] as num?)?.toDouble() ?? 96.0,
      scaleFactor: (map['scaleFactor'] as num?)?.toDouble() ?? 1.0,
      refreshRate: (map['refreshRate'] as int?) ?? 60,
      boundsLeft: (map['boundsLeft'] as int?) ?? 0,
      boundsTop: (map['boundsTop'] as int?) ?? 0,
      boundsRight: (map['boundsRight'] as int?) ?? 0,
      boundsBottom: (map['boundsBottom'] as int?) ?? 0,
      workLeft: (map['workLeft'] as int?) ?? 0,
      workTop: (map['workTop'] as int?) ?? 0,
      workRight: (map['workRight'] as int?) ?? 0,
      workBottom: (map['workBottom'] as int?) ?? 0,
    );
  }

  int get boundsWidth => boundsRight - boundsLeft;
  int get boundsHeight => boundsBottom - boundsTop;
  int get workWidth => workRight - workLeft;
  int get workHeight => workBottom - workTop;

  @override
  String toString() =>
      'Display(${boundsWidth}x$boundsHeight @${scaleFactor}x ${refreshRate}Hz${isPrimary ? " PRIMARY" : ""})';
}
