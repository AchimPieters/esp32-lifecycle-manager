# Acrobat Plugin Integratie-Architectuur

## Doel

Imposr Pro als native Acrobat plugin aanbieden met een TypeScript-gestuurde imposition kern.

## SDK Bridge

- Bridge module: `src/main/acrobat/AcrobatSDKBridge.ts`
- Lifecycle orchestratie: `src/main/acrobat/PluginLifecycleManager.ts`

## Lifecycle

1. Host load → `initialize()`
2. Menu registration → `registerMenu()`
3. User action → `executeImposition()`
4. Host unload → `shutdown()`

## Veiligheid

- Strikte validatie op configuratie en request payloads.
- Duidelijke foutcodes via `ImposrError`.
