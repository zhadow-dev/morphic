// A minimal, valid smoke test for the Notes Workspace example.
import 'package:flutter_test/flutter_test.dart';
import 'package:notes_workspace/main.dart';

void main() {
  test('NotesApp declares its three surfaces', () {
    final specs = NotesApp().surfaces();
    expect(specs.length, 3);
    expect(specs.map((s) => s.id), containsAll(['list', 'editor', 'inspector']));
  });
}
