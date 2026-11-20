# Security

Report suspected vulnerabilities privately to support@researchanddesire.com.
Include the firmware version, the board (R2 or R8) if known, a minimal
reproduction, and the expected impact. Do not post credentials, Wi-Fi
passwords, session details, device identifiers (MAC address, device ID or
pairing QR codes) or unredacted serial logs in public issues. Test only on a
box and an account that are your own, never on a box someone is relying on to
open, and never against another person's device or live account.

We most want to hear about: a way to open a locked box, or to keep one locked
when it should open (emergency unlock included), through firmware or hardware;
how the firmware handles MQTT commands, dashboard answers, Bluetooth and the
open Wi-Fi setup portal; firmware updates and their signed provenance; and
device identity and pairing. The
[device identity notes](software/firmware/lib/LockboxSecurity/README.md)
describe these protections and their known limit: the chip has no secure boot
or flash encryption, so someone holding the box with a USB-C cable can read its
flash and install other firmware. Keys are per device, so this exposes only the
box in hand.

Development builds talk only to services on the developer's own machine, set up
by `software/dev/` with generated credentials. Staging and production builds
verify the TLS certificate of every server they connect to, and accept a
firmware update only from R+D's own storage over HTTPS after checking its size
and SHA-256. The public keys in `FirmwareProvenance.h` identify signed
releases, and the keys and vectors in the test suites are public test data;
none of them are device credentials. Access to an official service remains
subject to that service's current authorization requirements.
