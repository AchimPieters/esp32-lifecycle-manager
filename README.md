# Imposr Pro — Adobe Acrobat Imposition Plugin

Imposr Pro is a commercial-grade PDF imposition platform for Adobe Acrobat workflows,
conceptually aligned with products like Quite Imposing.

This repository has been cleaned up to remove unrelated ESP32 firmware sources.
The active application code now lives in the `imposr/` workspace.

## Workspace

- Core engine: `imposr/src/core`
- Renderer/UI: `imposr/src/renderer`
- CLI: `imposr/src/cli`
- API: `imposr/src/api`
- Licensing/Updater/Analytics: `imposr/src/licensing`, `imposr/src/updater`, `imposr/src/analytics`

## Development

```bash
cd imposr
npm install
npm run test:coverage -- --runInBand
npm run typecheck
npm run lint
```
