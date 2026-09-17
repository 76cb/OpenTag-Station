# FilaBridge and Prusa XL setup

Select the intended Prusa XL and verify toolhead numbering before assigning a real spool.

## Before you start

FilaBridge configured for the printer and reachable on the trusted network. The adapter has contract coverage for v1.2.1 and v1.2.2; live write acceptance still requires the actual deployment.

## Steps

1. Configure and verify the printer in FilaBridge first. Ensure its status and toolhead mappings are coherent.
2. In station Settings → Integrations, enter the FilaBridge base URL and any reverse-proxy authentication needed locally.
3. Test discovery and choose the stable printer ID. The public fixture uses **Prusa XL**, not a personal device name.
4. Open the Printer view and compare T1–T5 with actual mappings. UI T1 corresponds to backend ID 0; T5 corresponds to ID 4.
5. Review local toolhead profiles and disable unavailable tools. When idle, perform one controlled assignment and verify the exact backend readback.

## Expected result

The selected stable printer and tool slots match FilaBridge. Assignment confirms only after a fresh mapping readback, not just the POST result.

## If it fails

Unknown versions may remain readable while guarded write capability is unavailable. A proxy, changed API shape, active printing or stale mapping can refuse assignment. Do not reuse an old printer revision after changing selection. See [assignment troubleshooting](../troubleshooting/assignment.md).
