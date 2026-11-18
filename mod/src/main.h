// What main.cpp gives the other units. Read through mod.h, which brings the headers in their order.
#ifndef MW4VR_MAIN_H
#define MW4VR_MAIN_H

int hookContext(ID3D11DeviceContext *ctx, bool deferred, bool repair);
void mouseInstall();
// In a battle the cursor is the mouse's stick: held to one side it runs out of the game's window, and
// the window under it then takes the input. While the game's window is in front and a battle runs the
// cursor is kept inside the window ([vr] mouse_capture=0: not). Who is in front is logged when it changes.
void mouseCapture();
void menuModeUpdate();

#endif // MW4VR_MAIN_H
