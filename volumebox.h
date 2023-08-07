#ifndef __WEBOSDPAGE_VOLUMEBOX_H
#define __WEBOSDPAGE_VOLUMEBOX_H

#include <vdr/skins.h>

class cWebOSDPageVolumeBox {
private:
  cOsd *osd;
  cPixmap *pixmap;
  int x0, x1;
  int y0, y1;
  tColor clrVolumeBarUpper = clrRed;
  tColor clrVolumeBarLower = clrGreen;
public:
  cWebOSDPageVolumeBox(cOsd *Osd, const cRect &Rect);
  virtual ~cWebOSDPageVolumeBox();
  void SetVolume(int Current = 0, int Total = 0, bool Mute = false);
  };

#endif //__WEBOSDPAGE_VOLUMEBOX_H
