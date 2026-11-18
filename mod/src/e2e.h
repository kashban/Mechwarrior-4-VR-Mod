// What e2e.cpp gives the other units. Read through mod.h, which brings the headers in their order.
#ifndef MW4VR_E2E_H
#define MW4VR_E2E_H

extern bool g_e2eOn;
extern std::vector<std::pair<float, float>> g_e2eYaw;
// the head's turn to the right the plan asks for now, in degrees
float e2eYawNow();
// once for every picture the game presents (a paused battle's menu too): the plan's keys go down and up
void e2eKeysStep();
// once for every frame with both eyes, before shot.cpp's pictures are taken - and for every flat picture of a menu's
// screen (r is null then; the record's "flat" is 1 and both eyes have the one picture's colour)
void e2eFrame(ID3D11DeviceContext *ctx, ID3D11Texture2D *l, ID3D11Texture2D *r);

#endif // MW4VR_E2E_H
