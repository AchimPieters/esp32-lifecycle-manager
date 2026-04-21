import { runCli } from '../../src/cli';

describe('e2e: batch cli', () => {
  it('dispatches batch command', async () => {
    const result = await runCli('batch');
    expect(result).toBeTruthy();
  });
});
