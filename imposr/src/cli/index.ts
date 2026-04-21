import { runBatch } from './commands/batch';
import { runImpose } from './commands/impose';
import { runTemplates } from './commands/templates';
import { runValidate } from './commands/validate';
import { runWatch } from './commands/watch';

export type CliCommand = 'impose' | 'batch' | 'watch' | 'templates' | 'validate';

/**
 * CLI dispatcher entrypoint.
 */
export async function runCli(command: CliCommand): Promise<string | string[]> {
  switch (command) {
    case 'impose':
      return runImpose({ input: 'input.pdf', output: 'output.pdf' });
    case 'batch':
      return runBatch(['a.pdf']);
    case 'watch':
      return runWatch('/tmp');
    case 'templates':
      return runTemplates(['2up-a4-a3']);
    case 'validate':
      return runValidate('input.pdf');
    default:
      throw new Error(`Unknown command: ${command}`);
  }
}
