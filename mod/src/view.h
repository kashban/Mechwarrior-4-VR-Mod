// What view.cpp gives the other units. Read through mod.h, which brings the headers in their order.
#ifndef MW4VR_VIEW_H
#define MW4VR_VIEW_H

// data holds the buffer bytes [base, base + sz) - a whole buffer (base 0) or the box of a partial update
void eyeLayoutFrom(int kind, const uint8_t *data, UINT sz, UINT base = 0);
void eyeOnPresent();
extern HMODULE g_self;
std::string moduleOf(void *addr);
void dumpSlots(void *obj, const char *what);
void startCapture();
void endCapture();

#endif // MW4VR_VIEW_H
