// Internal transitions repaint only the pairing screen that is still active.
"init.preflight.pairingPrompt"_s + event<PairingCodeChanged> / drawInitPairingQR,
"pairing"_s + event<PairingCodeChanged> / drawPairingQR,
