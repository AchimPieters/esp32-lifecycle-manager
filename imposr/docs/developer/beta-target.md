# Imposr Beta Target (voorlopig einddoel)

## Doel

Een testbare **Commercial Beta** die echte gebruikers kunnen valideren op workflow, kwaliteit en stabiliteit.

## Meetbaar einddoel

- CLI readiness report (`runCli('beta')`) toont blockers en progress.
- End-to-end workflow is testbaar: import -> impose -> export.
- Licensing-flow (activeren/upgraden) is aanwezig in renderer.
- API security middleware en distributie-assets zijn aanwezig.

## Hoe jij deze beta nu test

1. Start checks via CLI command `beta`.
2. Bekijk de output `READY_FOR_BETA=true/false`.
3. Los eerst alle `missing:` paden op uit de report-regels.
4. Herhaal tot `READY_FOR_BETA=true`.

## Resultaat

Dit geeft een iteratieve beta-lus: snel testen, blocker fixen, opnieuw meten.
