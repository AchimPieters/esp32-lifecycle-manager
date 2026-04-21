# Acrobat installatie-readiness (status op 2026-04-21)

## Kort antwoord

**Nee, nog niet.** Je kunt de TypeScript/Electron code bouwen en testen, maar je kunt deze repository nog niet direct als native plugin installeren in Adobe Acrobat Pro.

## Waarom nog niet

1. De huidige Acrobat bridge (`AcrobatSDKBridge`) is een TypeScript-abstraction en geen native Acrobat SDK plugin-binary (`.api`/`.aip`) gebouwd met C++.
2. De lifecycle manager roept alleen de bridge-methodes aan en bevat geen host binding naar Acrobat plugin entrypoints.
3. De packaging helper kan nu wél manifest + payload-index stagen, maar assembleert nog geen echte Acrobat-compatibele plugin binaries.
4. De huidige installer scripts zijn beta-scripts en voeren nog geen echte packaging/signing/installatie uit.

## Wat moet er eerst gebeuren

- Native plugin project opzetten met Adobe Acrobat SDK (C++).
- Echte bridge tussen native plugin en Imposr core implementeren (IPC/ABI boundary).
- Platform-builds opleveren (native binaries):
  - Windows: signed `.api/.aip` + installer
  - macOS: signed/notarized plugin bundle
- Installatiepad naar Acrobat plugin directory automatiseren.
- End-to-end smoke test op echte Acrobat Pro omgeving toevoegen.

## Minimale "eerste installabele alpha" definitie

Een eerste installabele alpha is bereikt zodra:

1. `ImposrPlugin` zichtbaar is in Acrobat menu.
2. Eén test-PDF via menuactie een output-PDF genereert.
3. Plugin load/unload geen crashes veroorzaakt.
4. Installatie en rollback scriptbaar is op minimaal één platform.
