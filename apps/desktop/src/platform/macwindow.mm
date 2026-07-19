#include "macwindow.hpp"

#import <AppKit/AppKit.h>

#include <QWidget>

namespace macwindow {

void hideTitleBar(QWidget* window) {
    NSView* view = reinterpret_cast<NSView*>(window->winId());
    NSWindow* nswindow = view.window;
    if (!nswindow)
        return;
    nswindow.titleVisibility = NSWindowTitleHidden;
    nswindow.titlebarAppearsTransparent = YES;
    nswindow.styleMask |= NSWindowStyleMaskFullSizeContentView;
}

}  // namespace macwindow
