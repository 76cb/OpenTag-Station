# Wi-Fi and browser access

Recover network access without erasing the station.

## Before you start

Physical access to the station and intended Wi-Fi credentials.

## Steps

1. Check power and normal display status. Inspect its assigned address rather than relying on an old browser bookmark.
2. After repeated saved-network failure, join OpenTag-Setup-XXXX and open the fixed setup address http://192.168.4.1/.
3. Correct SSID/password and choose Save and connect. Wait for reported permanent-network connection.
4. Reconnect the client to the permanent network and open the newly reported address. If mDNS fails, use that address directly.

## Expected result

The browser reconnects and the temporary AP closes after the success grace period.

## If it fails

VLAN isolation, captive portals or a client on a different subnet can prevent access. Recovery provisioning cannot replace an existing local API token. Preserve configuration unless USB recovery is actually necessary.
