#pragma once

class QWidget;

namespace macwindow {

// Removes the gray title bar strip: the title text is hidden and the
// content extends to the very top of the window, leaving only the
// three traffic-light buttons floating over it (macOS only; no-op on
// other platforms).
void hideTitleBar(QWidget* window);

// Moves the floating traffic-light buttons so their row starts at
// `leftMargin` and is vertically centered on `centerY` (both in points
// from the window's top-left corner). AppKit re-lays the buttons out
// on its own occasions, so call this again after window resizes.
void layoutTrafficLights(QWidget* window, int leftMargin, int centerY);

}  // namespace macwindow
