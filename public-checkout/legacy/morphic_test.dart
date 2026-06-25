import 'package:flutter_test/flutter_test.dart';
import 'package:morphic/morphic.dart';

void main() {
  test('SurfaceConfig toMap serialization', () {
    const config = SurfaceConfig(
      x: 100,
      y: 200,
      width: 400,
      height: 300,
      color: 0xE94560,
      elevation: 2,
    );

    final map = config.toMap();
    expect(map['x'], 100);
    expect(map['y'], 200);
    expect(map['width'], 400);
    expect(map['height'], 300);
    expect(map['color'], 0xE94560);
    expect(map['elevation'], 2);
  });

  test('DisplayInfo fromMap parsing', () {
    final info = DisplayInfo.fromMap({
      'isPrimary': true,
      'dpiX': 144.0,
      'dpiY': 144.0,
      'scaleFactor': 1.5,
      'refreshRate': 144,
      'boundsLeft': 0,
      'boundsTop': 0,
      'boundsRight': 2560,
      'boundsBottom': 1440,
      'workLeft': 0,
      'workTop': 0,
      'workRight': 2560,
      'workBottom': 1392,
    });

    expect(info.isPrimary, true);
    expect(info.scaleFactor, 1.5);
    expect(info.refreshRate, 144);
    expect(info.boundsWidth, 2560);
    expect(info.boundsHeight, 1440);
    expect(info.workHeight, 1392);
  });

  test('SurfaceGroup holds member IDs', () {
    final group = SurfaceGroup(id: 1, memberIds: [2, 3, 4]);
    expect(group.memberIds.length, 3);
    expect(group.memberIds, contains(3));
  });

  test('MorphicController starts uninitialized', () {
    final controller = MorphicController();
    expect(controller.isInitialized, false);
  });
}
