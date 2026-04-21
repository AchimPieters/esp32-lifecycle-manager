import { ImposeController } from '../../../src/api/controllers/ImposeController';
import { JobController } from '../../../src/api/controllers/JobController';
import { TemplateController } from '../../../src/api/controllers/TemplateController';

describe('api controllers', () => {
  it('returns controller responses', async () => {
    await expect(new ImposeController().impose({ pages: 2 })).resolves.toEqual({
      status: 'queued',
      pages: 2
    });
    await expect(new TemplateController().list()).resolves.toContain('2up-a4-a3');
    await expect(new JobController().getStatus('job-1')).resolves.toEqual({
      id: 'job-1',
      status: 'processing'
    });
  });

  it('throws for invalid controller input', async () => {
    await expect(new ImposeController().impose({ pages: 0 })).rejects.toThrow();
    await expect(new JobController().getStatus('')).rejects.toThrow();
  });
});
