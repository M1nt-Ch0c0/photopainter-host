# PhotoPainter board display service

The E6 and AXP2101 sources are extracted unchanged from photoframe commit
8c2de88. The source files retain their original MIT attribution and upstream
Waveshare commit. This component is built into the host for ABI 2 applications;
it never decodes PNG or knows application identities. The wrapper validates an
entire 800x480 logical indexed frame, then performs the existing 180-degree
packing and calls the verified driver. ABI 1 retains its own legacy driver;
the application manager serializes their use and no bus is held between calls.
