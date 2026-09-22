# Linux window controls

`SetClosable`, `SetMinimizable`, `SetMaximizable`, and `SetMovable` control
user-facing window capabilities. They do not disable programmatic window actions.
The settings persist on the native window and are shared by its nativeapi wrappers.

- **Closable:** uses GTK's `deletable` property to hide the CSD close button and
  request removal of the window manager's close control. `IsClosable` reads the
  GTK property. This is not a veto of `delete-event`, a process termination, or a
  compositor's force-close command.
- **Minimizable / maximizable:** filter the matching buttons from GTK header bars,
  preserving their original layout. X11 also receives the corresponding Motif
  function hints. Restoring both settings restores the original header layout,
  including inheritance from desktop settings.
- **Movable:** sends the X11 Motif move-function hint. GTK title-bar dragging and
  Wayland compositor movement are not disabled by this hint.

All function hints are combined, including GTK's resizable/deletable properties,
so changing one capability does not reset another. They are reapplied after
mapping and layout changes, including GTK-created header bars.

## Limits

X11 window managers may ignore Motif function hints. Wayland has no equivalent
protocol for disabling move, minimize, maximize, or close actions. Filtering CSD
buttons does not disable GTK title-bar double/middle-click actions, the title-bar
menu, compositor shortcuts, or custom controls supplied by an embedding application.
Applications needing stricter CSD interaction policies must implement their own
title bar and gate those actions themselves. Getters for movable/minimizable/
maximizable report the requested policy, not compositor acknowledgement.

No global GTK settings, window type, resizable property, or application content
are changed to approximate these capabilities. In particular, disabling maximize
does not disable programmatic resizing or change taskbar grouping.

References: [GDK function hints](https://docs.gtk.org/gdk3/method.Window.set_functions.html),
[GTK deletable](https://docs.gtk.org/gtk3/method.Window.set_deletable.html),
[GTK header layout](https://docs.gtk.org/gtk3/property.HeaderBar.decoration-layout.html).

## Showing and restoring minimized windows

`Show()` and `Restore()` clear GTK's pending iconify-on-map state and present the
window through GTK. On X11, calls outside an input event obtain a fresh X server
timestamp, so Mutter does not receive a stale activation timestamp.
`ShowInactive()` retains its non-activating behavior.

Minimized/restored events follow observed `window-state-event` transitions, not
requests. Pending iconify notifications are reconciled after queued GTK events
are processed, so transient remap state does not produce a false restore/minimize
pair. Repeated observations of the same iconified state do not emit another event. `IsMinimized()` likewise reads GDK's observed state.

GTK 3's Wayland backend does not report iconified state: `IsMinimized()` and the
minimize/restore events cannot confirm minimization on that backend. Presentation
is requested through GTK, but activation remains subject to compositor policy
and the available input serial/activation token. No fake state or events are
generated to hide this protocol limitation.

Desktop regression test (requires an active monitor and window manager):

```sh
cmake --build build --target window_restore_linux_test
GDK_BACKEND=x11 ./build/tests/window_restore_linux_test
GDK_BACKEND=wayland ./build/tests/window_restore_linux_test
```

It covers nativeapi minimization, GTK's title-bar iconify path, and hiding an
iconified window before showing it again. The Wayland checks use activation as
an observable restoration signal. Activation denied without a user token is
reported as a skipped restoration check, not successful restoration.
