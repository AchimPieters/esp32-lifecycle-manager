/** @jest-environment jsdom */
import { renderHook, act } from '@testing-library/react';
import { usePDF } from '../../../src/renderer/hooks/usePDF';
import { useTemplate } from '../../../src/renderer/hooks/useTemplate';
import { useBatch } from '../../../src/renderer/hooks/useBatch';
import { useLicense } from '../../../src/renderer/hooks/useLicense';
import { useSettings } from '../../../src/renderer/hooks/useSettings';

describe('renderer hooks', () => {
  it('updates hook states with validation', () => {
    const pdf = renderHook(() => usePDF());
    const template = renderHook(() => useTemplate());
    const batch = renderHook(() => useBatch());
    const license = renderHook(() => useLicense());
    const settings = renderHook(() => useSettings());

    act(() => {
      pdf.result.current.setPdfPath('doc.pdf');
      template.result.current.setTemplateId('tpl');
      batch.result.current.setJobs(2);
      license.result.current.setTier('pro');
      settings.result.current.setDarkMode(true);
    });

    expect(pdf.result.current.pdfPath).toBe('doc.pdf');
    expect(template.result.current.templateId).toBe('tpl');
    expect(batch.result.current.jobs).toBe(2);
    expect(license.result.current.tier).toBe('pro');
    expect(settings.result.current.darkMode).toBe(true);

    expect(() => pdf.result.current.setPdfPath('')).toThrow();
    expect(() => template.result.current.setTemplateId('')).toThrow();
    expect(() => batch.result.current.setJobs(-1)).toThrow();
    expect(() => license.result.current.setTier('')).toThrow();
  });
});
