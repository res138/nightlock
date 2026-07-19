#pragma once

class QWidget;

namespace macwindow {

// Removes the gray title bar strip: the title text is hidden and the
// content extends to the very top of the window, leaving only the
// three traffic-light buttons floating over it (macOS only; no-op on
// other platforms).
void hideTitleBar(QWidget* window);

}  // namespace macwindow
