#include "macwindow.hpp"

#import <AppKit/AppKit.h>

#include <QWidget>

namespace macwindow {

void hideTitleBar(QWidget* window) {
    // The client area itself is extended by Qt::ExpandedClientAreaHint /
    // Qt::NoTitleBarBackgroundHint; this only hides the title text.
    NSView* view = reinterpret_cast<NSView*>(window->winId());
    NSWindow* nswindow = view.window;
    if (!nswindow)
        return;
    nswindow.titleVisibility = NSWindowTitleHidden;
}

}  // namespace macwindow
