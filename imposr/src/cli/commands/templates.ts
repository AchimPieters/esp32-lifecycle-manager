import { CliLogger } from '../utils/logger';

/**
 * Lists available template identifiers.
 */
export function runTemplates(templateIds: string[], logger = new CliLogger()): string {
  if (templateIds.length === 0) {
    throw new Error(logger.error('No templates available'));
  }

  return logger.info(`Templates: ${templateIds.join(', ')}`);
}
