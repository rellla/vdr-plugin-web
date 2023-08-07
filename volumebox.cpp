#include "volumebox.h"

cWebOSDPageVolumeBox::cWebOSDPageVolumeBox(cOsd *Osd, const cRect &Rect) {
  osd = Osd;
  pixmap = osd->CreatePixmap(7, Rect);
  pixmap->Fill(clrTransparent);

  x0 = 0;
  x1 = Rect.Width();
  y0 = 0;
  y1 = Rect.Height();
}

cWebOSDPageVolumeBox::~cWebOSDPageVolumeBox() {
  osd->DestroyPixmap(pixmap);
}

void cWebOSDPageVolumeBox::SetVolume(int Current, int Total, bool Mute) {
  if (Mute) {
    pixmap->DrawRectangle(cRect(x0, y0, x1 - 1, y1 - 1), clrVolumeBarUpper);
  } else {
    int p = (x1 - 1) * Current / Total;
    pixmap->DrawRectangle(cRect(x0, y0, p - 1, y1 - 1), clrVolumeBarLower);
    pixmap->DrawRectangle(cRect(p, y0, x1 - 1, y1 - 1), clrVolumeBarUpper);
  }
}
