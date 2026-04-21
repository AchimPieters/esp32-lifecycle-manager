# Packaging Plan (Acrobat Plugin)

## Deliverables

- Windows package: signed ZIP/MSI wrapper met plugin payload
- macOS package: signed ZIP/PKG wrapper met plugin payload

## Build helper

- Script: `scripts/deploy/package-acrobat-plugin.ts`
- Doel: consistente artifact naming en release-automatisering

## Release flow

1. Build plugin bridge binaries + renderer assets
2. Assemble platform package
3. Sign artifact
4. Publish release metadata
