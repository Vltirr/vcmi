# Trivial Stack Retaliation

Optional retaliation rules configured through game settings under combat.trivialStackRetaliation.

- enabled: boolean, default false.
- thresholdPercent: integer from 0 to 100, default 10. An attacking stack is trivial when its creature AI value multiplied by its count is at most this percentage of the defender's power, measured before each hit.
- Damage: use full normal retaliation damage from the surviving defenders and their current bonuses. Power affects consumption only; there is no damage scaling or additional rounding.
- Availability: existing retaliation conditions and remaining uses apply. A trivial response preserves its use; an ordinary response consumes it. A dead defender cannot respond.
- Multiple hits: after a free response, compare power again before the next hit. A later non-trivial response consumes a use; this does not grant extra responses after an ordinary response.
- Zero or invalid creature power: use ordinary retaliation rules.
- Secondary victims: normal retaliation damage applies. Collateral victims of the incoming attack gain no response.
- Other spell and combat-event effects keep their existing rules and are not undone when a creature dies.
- Saves retain the effective configuration. Saves that predate this setting load with the mechanic disabled. Select gameplay modules before starting a new game.
- The obsolete prototype damagePercent field is ignored when loading saved settings; it cannot reduce retaliation damage.

```json
{
  "combat": {
    "trivialStackRetaliation": {
      "enabled": true,
      "thresholdPercent": 10
    }
  }
}
```
