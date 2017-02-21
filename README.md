# Remote Desktop Screen Lock Inhibitor

This is a small utility application to inhibit screen lock only when connected over a Windows Remote Desktop connection, because inactivity timeouts on a virtual desktop are annoying, but screensaver configuration is only per-user or per-machine granularity.

The application sits in the system tray, and detects when the session changes between local and remote. When remote, the application will inject input events at an interval slightly shorter than the screen saver timeout. The context menu of the application provides mechanism for switching between the two supported inhibition mechanisms: null mouse movements, or refreshing the current screen saver timeout.

The included icons are copied/derivative from http://iconmonstr.com, specifically keyboard-9, lock-20, computer-6, and cloud-15.
