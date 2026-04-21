import { createInitialStore } from '../../../src/renderer/store';
import { setBatchJobs } from '../../../src/renderer/store/slices/batchSlice';
import { setLicenseTier } from '../../../src/renderer/store/slices/licenseSlice';
import { setPdfPath } from '../../../src/renderer/store/slices/pdfSlice';
import { setDarkMode } from '../../../src/renderer/store/slices/settingsSlice';
import { setTemplate } from '../../../src/renderer/store/slices/templateSlice';
import { licenseMiddleware } from '../../../src/renderer/store/middleware/licenseMiddleware';
import { formatKilobytes } from '../../../src/renderer/utils/formatters';
import { buildOutputName } from '../../../src/renderer/utils/helpers';
import { isPdfPath } from '../../../src/renderer/utils/validators';

describe('renderer store and utils', () => {
  it('creates and updates store slices', () => {
    const store = createInitialStore();

    expect(setPdfPath(store.pdf, 'x.pdf').path).toBe('x.pdf');
    expect(setTemplate(store.template, 'tpl').id).toBe('tpl');
    expect(setBatchJobs(store.batch, 3).jobs).toBe(3);
    expect(setLicenseTier(store.license, 'pro').tier).toBe('pro');
    expect(setDarkMode(store.settings, true).darkMode).toBe(true);
    expect(licenseMiddleware('pro')).toBe('pro');

    expect(() => setPdfPath(store.pdf, '')).toThrow();
    expect(() => setBatchJobs(store.batch, -1)).toThrow();
    expect(() => setLicenseTier(store.license, '')).toThrow();
    expect(() => licenseMiddleware('')).toThrow();
  });

  it('uses renderer utility helpers', () => {
    expect(formatKilobytes(2048)).toBe('2.00 KB');
    expect(isPdfPath('file.PDF')).toBe(true);
    expect(buildOutputName('report')).toBe('report-imposed.pdf');
    expect(() => formatKilobytes(-1)).toThrow();
    expect(() => buildOutputName('')).toThrow();
  });
});
