# Imposr Pro — wat moet nog gebeuren voor real-life gebruik?

Laatst bijgewerkt: 2026-04-21 (UTC)

## 1) Productfunctionaliteit naar prepress-niveau brengen

- Volledige Acrobat-plugin runtime integreren (native bridge + plugin entrypoints).
- Imposition verdiepen naar Quite Imposing-niveau:
  - signature/shuffle-assistent
  - manual imposition workflow
  - geavanceerde creep/trim presets per drukproces
- PDF preflight uitbreiden:
  - PDF/X compliance checks
  - output-intent/behoud van metadata
  - fail-fast validatie vóór batchverwerking

## 2) UI/UX en workflow-compleetheid

Nog te bouwen renderer-domeincomponenten:

- `src/renderer/components/pdf/PDFViewer.tsx`
- `src/renderer/components/pdf/PDFThumbnail.tsx`
- `src/renderer/components/pdf/PDFRenderer.tsx`
- `src/renderer/components/pdf/PageNavigator.tsx`
- `src/renderer/components/templates/TemplateSelector.tsx`
- `src/renderer/components/templates/TemplatePreview.tsx`
- `src/renderer/components/templates/TemplateEditor.tsx`
- `src/renderer/components/templates/TemplateLibrary.tsx`
- `src/renderer/components/batch/BatchJobList.tsx`
- `src/renderer/components/batch/JobProgress.tsx`
- `src/renderer/components/batch/BatchSettings.tsx`

Definition of done voor UI:

- End-to-end flows voor import → templatekeuze → preview → impose → export.
- Foutmeldingen met herstelacties (geen kale exceptions naar eindgebruiker).
- UX-validatie met minimaal 5 echte print/prepress gebruikers.

## 3) Security, API en enterprise hardening

Nog te implementeren API-middleware:

- `src/api/middleware/auth.ts`
- `src/api/middleware/rateLimit.ts`
- `src/api/middleware/validation.ts`
- `src/api/middleware/errorHandler.ts`

Daarnaast noodzakelijk:

- Secrets/config centraliseren (`src/utils/config.ts`).
- Veilige persistente storage (`src/utils/storage.ts`).
- Cryptografische helpers en key-rotatiestrategie (`src/utils/crypto.ts`).
- Audit logging met correlatie-id per API/CLI job.

## 4) Templates en kwaliteitsborging

Nog ontbrekende standaardtemplates:

- `templates/standard/4up-a4-a3.json`
- `templates/standard/8up-a4-a2.json`
- `templates/standard/booklet-8page.json`
- `templates/standard/booklet-32page.json`
- `templates/standard/perfect-bound.json`

Kwaliteitsvereisten:

- Golden-master fixtures voor layout-vergelijking (visueel + numeriek).
- Regressietests met echte drukwerk-scenario's.
- Coverage-gate op CI (branches/functions/lines/statements >= 80%).

## 5) Distributie, signing en update-keten

Nog ontbrekende installer/release artefacten:

- `installers/windows/build.ps1`
- `installers/linux/rpm`
- `installers/linux/appimage`

Go-live eisen:

- Code-signing voor binaries/installers per platform.
- Reproduceerbare builds en SBOM per release.
- Rollback-procedure voor mislukte auto-updates.

## 6) Documentatie en support-operatie

Nog ontbrekende documentatie:

- `docs/user-guide/templates.md`
- `docs/user-guide/batch-processing.md`
- `docs/user-guide/troubleshooting.md`
- `docs/api-reference/rest-api.md`
- `docs/api-reference/cli-reference.md`
- `docs/developer/contributing.md`
- `docs/developer/plugin-development.md`
- `docs/developer/api-integration.md`

Operationele klaarheid:

- SLA/SLO-definitie (uptime, support-reactietijd, crash-rate).
- Incident runbooks (licensing outage, update-failure, corrupt PDF).
- Telemetry dashboards + alerting met on-call protocol.

## Praktisch advies: realistische releasevolgorde

1. **MVP beta (4-6 weken):** UI workflows + API hardening + 5 kerntemplates.
2. **Prepress beta (6-8 weken):** PDF/X en geavanceerde imposition-features.
3. **Commercial GA (2-4 weken):** signing, installers, support runbooks, docs freeze.

Zodra bovenstaande zes blokken afgerond zijn, is de plugin klaar voor gecontroleerde productie-uitrol bij echte klanten.
