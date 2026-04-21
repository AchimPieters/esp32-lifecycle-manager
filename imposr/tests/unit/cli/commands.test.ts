import { runBatch } from '../../../src/cli/commands/batch';
import { runImpose } from '../../../src/cli/commands/impose';
import { runTemplates } from '../../../src/cli/commands/templates';
import { runValidate } from '../../../src/cli/commands/validate';
import { runWatch } from '../../../src/cli/commands/watch';
import { runCli } from '../../../src/cli';

describe('cli commands', () => {
  it('runs command helpers', async () => {
    await expect(runImpose({ input: 'a.pdf', output: 'b.pdf' })).resolves.toContain('Imposed');
    await expect(runBatch(['a.pdf'])).resolves.toHaveLength(1);
    expect(runWatch('/tmp')).toContain('Watching');
    expect(runTemplates(['x'])).toContain('Templates');
    expect(runValidate('a.pdf')).toContain('Validated');
  });

  it('throws on invalid input', async () => {
    await expect(runImpose({ input: '', output: 'x' })).rejects.toThrow();
    await expect(runBatch([])).rejects.toThrow();
    expect(() => runWatch('')).toThrow();
    expect(() => runTemplates([])).toThrow();
    expect(() => runValidate('a.txt')).toThrow();
  });

  it('dispatches through runCli', async () => {
    await expect(runCli('impose')).resolves.toBeTruthy();
    await expect(runCli('batch')).resolves.toBeTruthy();
    await expect(runCli('watch')).resolves.toBeTruthy();
    await expect(runCli('templates')).resolves.toBeTruthy();
    await expect(runCli('validate')).resolves.toBeTruthy();
    await expect(runCli('beta')).resolves.toContain('READY_FOR_BETA=');
  });
});
