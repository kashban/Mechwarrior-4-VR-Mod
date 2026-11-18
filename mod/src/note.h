// What note.cpp gives the other units. Read through mod.h, which brings the headers in their order.
#ifndef MW4VR_NOTE_H
#define MW4VR_NOTE_H

void noteShow(const wchar_t *text, int seconds = 5);
// into the picture tex (w x h used of it, 8-bit BGRA), while a note is due
void noteDraw(ID3D11DeviceContext *ctx, ID3D11Texture2D *tex, UINT w, UINT h);

#endif // MW4VR_NOTE_H
