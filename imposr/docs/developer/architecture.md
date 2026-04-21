# Architecture

Imposr Pro follows a modular architecture:

- `src/core`: domain engines (pdf, imposition, templates, batch, marks)
- `src/main`: Electron main-process orchestration
- `src/api`: REST API server + routes/controllers
- `src/licensing`, `src/updater`, `src/analytics`: commercial platform modules
