import { CliLogger } from '../utils/logger';

/**
 * Validates command input path formatting.
 */
export function runValidate(path: string, logger = new CliLogger()): string {
  if (!path.toLowerCase().endsWith('.pdf')) {
    throw new Error(logger.error('Only .pdf files are supported'));
  }

  return logger.info(`Validated ${path}`);
}
