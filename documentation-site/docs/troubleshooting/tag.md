# Tag not detected

Confirm reader health before changing tag data.

## Before you start

One approved SLIX2 tag and access to normal diagnostics.

## Steps

1. Check NFC reader ready state and bus errors; an absent reader is a wiring/power problem before it is a tag problem.
2. Verify module I²C bridge, GPIO13 SDA, GPIO14 SCL and GPIO12 IRQ. Leave CS/BSS and MOSI disconnected.
3. Place only one tag close to the integrated antenna and move metal/load-cell hardware away from it.
4. Try another approved tag, then remove/reinsert and wait for stable detection.

## Expected result

Stable UID/presence and supported geometry appear; normal touch and scale remain responsive.

## If it fails

Random NTAG/MIFARE stickers are not the approved profile. A reader ACK at 0x50 alone does not prove RF/IRQ operation. See [supported tags](../openprinttag/supported-tags.md).
