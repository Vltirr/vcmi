# Trivial Stack Retaliation

Optional retaliation rules configured through game settings under combat.trivialStackRetaliation.

- enabled: boolean, default false.
- thresholdPercent: integer from 0 to 100, default 10. An attacking stack is trivial when its creature AI value multiplied by its count is at most this percentage of the defender's power, measured before each hit.
- damagePercent: integer from 0 to 1000000, default 100. Multiplier for proportional retaliation damage expressed as a percentage: 100 is ×1, 200 is ×2. Zero produces zero proportional damage without disabling the free response.
- Damage: calculate normal retaliation damage with surviving defenders, multiply by the pre-hit attacker/defender power ratio and damagePercent / 100, and round up. No further minimum is applied. Normal damage scripts still supply the unscaled damage.
- Availability: existing retaliation conditions and remaining uses apply. A trivial response preserves its use; an ordinary response consumes it. A dead defender cannot respond.
- Multiple hits: after a free response, compare power again before the next hit. A later non-trivial response consumes a use; this does not grant extra responses after an ordinary response.
- Zero or invalid creature power: use ordinary retaliation rules.
- Secondary victims: the proportional factor applies to the direct damage of the retaliation, including its secondary victims. Collateral victims of the incoming attack gain no response.
- Other spell and combat-event effects keep their existing rules and are not undone when a creature dies.
- Saves retain the effective configuration. Saves that predate this setting load with the mechanic disabled. Select gameplay modules before starting a new game.

```json
{
  "combat": {
    "trivialStackRetaliation": {
      "enabled": true,
      "thresholdPercent": 10,
      "damagePercent": 100
    }
  }
}
```
