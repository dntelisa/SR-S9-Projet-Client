#include <raylib.h>

int main(int argc, char *argv[]) {
  InitWindow(640, 480, "jeu");
  SetWindowState(FLAG_VSYNC_HINT);
  SetExitKey(KEY_ESCAPE);

  while (!WindowShouldClose()) {
    BeginDrawing();
    ClearBackground(WHITE);
    EndDrawing();
  }

  CloseWindow();

  return 0;
}
