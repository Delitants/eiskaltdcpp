# Legacy ADC Password Challenges

The ADC protocol requires a password challenge containing at least 24 random
bytes, encoded as unpadded Base32. Some older hubs send shorter, noncanonical
tokens instead. Strict validation rejects these challenges. Eiskalt now shows
an authentication error rather than silently waiting without a password prompt.

If a trusted hub requires the older behavior, edit that hub in **Favourite hubs**
and enable **Allow legacy ADC password challenges for this hub (less secure)**.
Save the favourite and reconnect. This option is off by default, including for
existing favourites without an explicitly saved choice. It does not apply to
quick connections without a matching favourite, or to other hub addresses.
Editing the address in the favourite editor clears the checkbox.

The compatibility exception accepts uppercase Base32 tokens representing at
least six complete bytes, permits nonzero unused tail bits, and retains command
length and allocation bounds. Invalid characters, padding, impossible Base32
byte lengths, and shorter tokens remain rejected. Password hash input order and
one-time challenge consumption are unchanged. A warning appears when the
exception is actually needed; normal challenges do not generate that warning.

This is a deliberate authentication compatibility downgrade, not a claim that
the hub complies with the specification. It does not disable TLS, certificate
pin verification, GOST authentication, or proxy routing. Prefer asking the hub
administrator to generate proper challenges. Existing connections keep their
policy until reconnect; periodic identity updates do not apply preference edits.

The choice is stored as the favourite's `LegacyAdcPasswordChallenges` attribute,
not as a global setting. No hub or proxy credentials are included in source.

Protocol reference: [ADC GPA/PAS](https://dc-protocols.github.io/ADC.html).
