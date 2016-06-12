# CXVideoCube

This is an early access release of code demonstrating rendering of hardware decoded video on a 3D surface for Odroid C0/C1/C2.  This is not a final release.  It is provided for other developers that expressed interest.

Currently, a driver patch to the amvideocap driver is required for proper operation.

This code uses the mali-fbdev driver package.  With minor modification, it should also work under X11.

Console blanking will affect the display.  If the spinning cube disappers after a specific amount of time, unblank the console to restore it.

