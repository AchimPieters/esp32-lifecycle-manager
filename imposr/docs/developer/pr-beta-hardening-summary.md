# PR samenvatting — Beta hardening en testgereedheid

Deze wijzigingsset bevat de volgende blokken:

1. **Betrouwbare CI + quality gates**
   - lint, typecheck, coverage als vaste pipeline.
2. **API hardening**
   - auth, rate-limit, validation, error handling.
3. **Beta readiness tooling**
   - beta doelmodel, readiness service, CLI command.
4. **Packaging baseline**
   - manifest + payload staging voor Acrobat packaging flow.
5. **Renderer + utilities + templates**
   - extra componenten, utility modules en standaard templates.
6. **Testuitbreiding**
   - unit tests voor API, renderer, scripts, core, utils.

Doel: gesloten beta-testen mogelijk maken met duidelijke Go/No-Go grenzen.
