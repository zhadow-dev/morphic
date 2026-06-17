/// A group of surfaces that move, resize, and elevate together.
class SurfaceGroup {
  final int id;
  final List<int> memberIds;

  SurfaceGroup({required this.id, required this.memberIds});
}
