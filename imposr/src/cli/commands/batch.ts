import { CliLogger } from '../utils/logger';

/**
 * Executes batch command over provided files.
 */
export async function runBatch(files: string[], logger = new CliLogger()): Promise<string[]> {
  if (files.length === 0) {
    throw new Error(logger.error('At least one file is required'));
  }

  return files.map((file) => logger.info(`Queued ${file}`));
}
