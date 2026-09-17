# First installation and USB recovery

Install the complete production factory image or recover an unreachable station over USB.

## Before you start

A supported WT32-SC01 Plus, stable power, data-capable USB cable and desktop Chrome or Edge with Web Serial. Back up configuration first if the station is reachable.

## Steps

1. Open the [OpenTag Station installer](https://76cb.github.io/OpenTag-Station/). Check the manifest version against the intended release. During PR review, main’s installer may still precede the candidate.
2. Connect the board by USB. Close serial monitors and other programs holding the device.
3. Choose **Install OpenTag Station**, select the correct USB port, and review the erase choice. A factory/erase recovery can remove configuration and calibration.
4. Let flashing and verification finish without disconnecting the board. Use board-manufacturer boot/download instructions if the port does not enter flashing mode automatically.
5. Reboot into normal OpenTag Station and verify version in About/boot output. Follow [first boot](../getting-started/first-boot.md) if configuration is absent.
6. Check reader, scale and touch using the normal production firmware. Restore nonsecret configuration and locally re-enter secrets as required.

## Expected result

The board boots the intended production version, serves the local UI and can complete normal setup. Only the production factory image belongs in the installer.

## If it fails

Missing port usually means cable, driver, permissions or another open serial program. Interrupted flashing requires retrying the production USB recovery path. Do not upload the merged offset-zero factory binary through OTA; OTA uses the application binary.
