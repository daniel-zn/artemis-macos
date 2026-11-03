#import <AppKit/AppKit.h>

// True unless macOS's pasteboard privacy would ask (or refuse) when this app reads
// the general pasteboard by itself. Reading accessBehavior doesn't prompt.
bool clipboardSyncMayReadPasteboardSilently()
{
    if (@available(macOS 15.4, *)) {
        return [NSPasteboard generalPasteboard].accessBehavior == NSPasteboardAccessBehaviorAlwaysAllow;
    }
    return true;
}
