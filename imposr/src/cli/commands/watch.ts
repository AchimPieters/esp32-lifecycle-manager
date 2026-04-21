import { CliLogger } from '../utils/logger';

/**
 * Builds a watch status message.
 */
export function runWatch(path: string, logger = new CliLogger()): string {
  if (!path.trim()) {
    throw new Error(logger.error('Watch path is required'));
  }

  return logger.info(`Watching ${path}`);
}
