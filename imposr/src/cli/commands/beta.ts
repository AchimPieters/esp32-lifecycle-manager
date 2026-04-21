import fs from 'node:fs';
import { BetaReadinessService, type BetaReadinessReport } from '../../beta/BetaReadinessService';

/**
 * Runs beta-readiness checks and returns a formatted report string.
 */
export function runBeta(): string {
  const service = new BetaReadinessService({ exists: (path) => fs.existsSync(path) });
  const report = service.evaluate();

  return formatReport(report);
}

/**
 * Formats a beta-readiness report for CLI output.
 */
export function formatReport(report: BetaReadinessReport): string {
  const header = `${report.goalName} (${report.completed}/${report.total} - ${report.progressPercent}%)`;
  const readiness = report.readyForBeta ? 'READY_FOR_BETA=true' : 'READY_FOR_BETA=false';

  const lines = report.results.map((result) => {
    const status = result.complete ? '✅' : '❌';
    const missing = result.missingPaths.length === 0 ? '' : ` | missing: ${result.missingPaths.join(', ')}`;
    return `${status} ${result.id}: ${result.description}${missing}`;
  });

  return [header, readiness, ...lines].join('\n');
}
