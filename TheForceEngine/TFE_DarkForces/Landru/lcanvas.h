#pragma once
//////////////////////////////////////////////////////////////////////
// Dark Forces
// Landru Canvas - this is drawn to by the system.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include "lrect.h"

namespace TFE_DarkForces
{
	void lcanvas_init(s16 w, s16 h);
	void lcanvas_destroy();

	void  lcanvas_getBounds(LRect* rect);
	void  lcanvas_setClip(LRect* rect);
	void  lcanvas_getClip(LRect* rect);
	JBool lcanvas_clipRectToCanvas(LRect* rect);
	void  lcanvas_clearClipRect();

	void  lcanvas_eraseRect(LRect* rect);
}  // namespace TFE_DarkForces