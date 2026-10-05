// Linked only into the disposable current-main acceptance fixture.
#import <AppKit/AppKit.h>
#include <cstdio>

bool fixtureRequestNativeTermination()
{
    if (!NSApp || ![NSApp isRunning] || ![NSApp delegate])
        return false;
    fprintf(stdout, "NATIVE_TERMINATION_REQUESTED=1\n");
    fflush(stdout);
    [NSApp terminate:nil];
    return true;
}
