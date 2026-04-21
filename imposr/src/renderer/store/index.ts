import { initialBatchState } from './slices/batchSlice';
import { initialLicenseState } from './slices/licenseSlice';
import { initialPdfState } from './slices/pdfSlice';
import { initialSettingsState } from './slices/settingsSlice';
import { initialTemplateState } from './slices/templateSlice';

/** Renderer store snapshot type. */
export interface RendererStore {
  readonly pdf: typeof initialPdfState;
  readonly template: typeof initialTemplateState;
  readonly batch: typeof initialBatchState;
  readonly license: typeof initialLicenseState;
  readonly settings: typeof initialSettingsState;
}

/** Creates initial renderer store state. */
export function createInitialStore(): RendererStore {
  return {
    pdf: initialPdfState,
    template: initialTemplateState,
    batch: initialBatchState,
    license: initialLicenseState,
    settings: initialSettingsState
  };
}
