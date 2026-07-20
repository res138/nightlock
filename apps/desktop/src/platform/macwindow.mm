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

void layoutTrafficLights(QWidget* window, int leftMargin, int centerY) {
    NSView* view = reinterpret_cast<NSView*>(window->winId());
    NSWindow* nswindow = view.window;
    // In native fullscreen the buttons belong to the auto-hiding bar.
    if (!nswindow || (nswindow.styleMask & NSWindowStyleMaskFullScreen))
        return;
    NSButton* buttons[] = {[nswindow standardWindowButton:NSWindowCloseButton],
                           [nswindow standardWindowButton:NSWindowMiniaturizeButton],
                           [nswindow standardWindowButton:NSWindowZoomButton]};
    // The theme frame spans the whole window; positions are computed
    // against it and converted into each button's own superview, which
    // handles flipped coordinates and container offsets in one go.
    NSView* frameView = nswindow.contentView.superview;
    // The native pitch is 20pt; a touch more air between the buttons.
    constexpr CGFloat kPitch = 22;
    CGFloat x = leftMargin;
    for (NSButton* button : buttons) {
        if (!button)
            continue;
        const NSRect frame = button.frame;
        const CGFloat fromTop = centerY - frame.size.height / 2.0;
        const NSPoint inFrame =
            NSMakePoint(x, frameView.isFlipped
                               ? fromTop
                               : frameView.bounds.size.height - fromTop - frame.size.height);
        [button setFrameOrigin:[button.superview convertPoint:inFrame fromView:frameView]];
        x += kPitch;
    }
}

}  // namespace macwindow
