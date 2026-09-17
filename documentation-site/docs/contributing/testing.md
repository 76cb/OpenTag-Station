# Testing and CI

Firmware CI has independent UI-review, documentation and firmware jobs. The
firmware job runs native tests, ASan/UBSan writer and Weigh tests, deterministic
Node tests, Chromium at 1440/1280/1024/768/390 pixels, upstream codec/reference
checks, the production build, ELF ownership checks, internal-RAM limits and
backend/HTTP/OTA stack reserves. It then validates the production factory bundle.

The review job builds offline fixtures from shipped browser assets and production
WT32 coordinates. It captures 65 browser views and six touch-layout approximations.
Before every browser screenshot, visible body text and HTML are checked for the
prohibited personal identifier. Every WT32 text input is checked before drawing.
Source and generated text/metadata are checked before upload; failed jobs do not
upload review artifacts. Curated committed images carry SHA-256 provenance.

Documentation CI runs strict MkDocs, privacy, image provenance and every local
link/image/anchor check. Firmware CI also compares hardware documentation and
generated SVGs against production GPIO, address, bus and tag-profile contracts.
All Actions use commit pins. Main in the separate documentation repository deploys
only that documentation site; firmware Pages remains the installer.

When changing a safety path, add meaningful refusal/recovery tests and retain
existing production coverage. When changing generated UI, regenerate assets and
capture fixtures. Review actual browser views and actual WT32 hardware separately:
fixtures and passing tests do not prove touch calibration, color fidelity, live
service writes or power-loss recovery. Record those in the [release checklist](release.md).
