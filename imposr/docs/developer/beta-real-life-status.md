# Beta real-life test status (2026-04-21)

## Kort antwoord

**Ja, voor een gesloten beta. Nee, nog niet voor een publieke Acrobat-pilot.**

- ✅ **Klaar om nu te testen** met interne testers op functionaliteit, stabiliteit en workflow.
- ⚠️ **Nog niet klaar** voor klantuitrol als native Adobe Acrobat Pro plugin.

## Bewijs van huidige technische staat

- `npm test -- --runInBand` slaagt.
- `npm run test:coverage -- --runInBand` slaagt met ingestelde globale coverage thresholds.
- `npm run typecheck` slaagt.
- `npm run lint` slaagt.

## Wat je nu wel kunt doen

1. Start een gesloten testgroep (intern of design-partners).
2. Test de volledige flow: import → template → impose → export.
3. Test ook API middleware-paden (auth/rate-limit/validatie).
4. Verzamel issues in drie buckets: blockers, bugs, UX verbeteringen.

## Wat nog ontbreekt voor echte Acrobat praktijkpilot

- Native Acrobat SDK plugin-binaries (`.api/.aip`) per platform.
- Productieklare signing/notarization/installatieketen.
- Acrobat-host integratie met echte plugin entrypoints.

## Go/No-Go advies

- **Go** voor gesloten beta-tests deze week.
- **No-Go** voor publieke/kunde-pilot totdat native Acrobat keten is afgerond.
