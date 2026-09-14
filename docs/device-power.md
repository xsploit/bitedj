# Device power controls

Settings → System → Power offers **Shut Down Device**, **Restart Device**, and
Cancel. Each device action opens a separate confirmation page. **Restart BiteDJ**
remains an application-only action and uses exit code 42, supported by the PiFlex
supervisor.

The device actions use systemd/logind on Linux. BiteDJ reserves logind's shutdown
delay window before sending `systemctl --no-ask-password poweroff` or `reboot`.
On `PrepareForShutdown`, BiteDJ exits through its normal cleanup so recordings,
loaded-track metadata and settings can be saved. The delay descriptor remains
open until application destruction; logind still enforces its own maximum delay.

No root shell, forced shutdown, inhibitor override, or password prompt is used.
The graphical session must already have logind permission. A missing service,
refused command, or 15-second command timeout displays an error and leaves the
application open. Repeated taps cannot submit concurrent power requests.

## Validation

- Desktop Release build succeeded.
- 111 existing system/library/Engine tests passed.
- Full application UI test with a substitute `systemctl` that always refuses:
  cancel sends no command; reboot and shutdown send their distinct arguments;
  each refusal restores the normal power menu and permits another request.
- Visually checked System, power menu, confirmation and failure pages at 1280×800.
- Pi touchscreen session reports logind `CanPowerOff = yes`.

The private Engine model and firmware files are not part of this change.
This build does not add stem separation or stem playback to BiteDJ.
