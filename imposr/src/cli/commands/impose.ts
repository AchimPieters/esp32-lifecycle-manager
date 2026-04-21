import { CliLogger } from '../utils/logger';

export interface ImposeOptions {
  readonly input: string;
  readonly output: string;
}

/**
 * Executes a single imposition command.
 */
export async function runImpose(options: ImposeOptions, logger = new CliLogger()): Promise<string> {
  if (!options.input.trim() || !options.output.trim()) {
    throw new Error(logger.error('Input and output are required'));
  }

  return logger.info(`Imposed ${options.input} -> ${options.output}`);
}
