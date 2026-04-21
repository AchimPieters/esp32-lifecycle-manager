import { formatReport } from '../../../src/cli/commands/beta';
import { type BetaReadinessReport } from '../../../src/beta/BetaReadinessService';

describe('beta cli command formatting', () => {
  it('formats success report', () => {
    const report: BetaReadinessReport = {
      goalName: 'Imposr Beta',
      targetDate: '2026-06-30',
      completed: 2,
      total: 2,
      progressPercent: 100,
      readyForBeta: true,
      results: [
        { id: 'a', description: 'A', complete: true, missingPaths: [] },
        { id: 'b', description: 'B', complete: true, missingPaths: [] }
      ]
    };

    const output = formatReport(report);
    expect(output).toContain('READY_FOR_BETA=true');
    expect(output).toContain('✅ a: A');
  });

  it('formats missing paths in report', () => {
    const report: BetaReadinessReport = {
      goalName: 'Imposr Beta',
      targetDate: '2026-06-30',
      completed: 1,
      total: 2,
      progressPercent: 50,
      readyForBeta: false,
      results: [
        { id: 'a', description: 'A', complete: true, missingPaths: [] },
        { id: 'b', description: 'B', complete: false, missingPaths: ['x.ts'] }
      ]
    };

    const output = formatReport(report);
    expect(output).toContain('READY_FOR_BETA=false');
    expect(output).toContain('missing: x.ts');
  });
});
