import { BetaReadinessService } from '../../../src/beta/BetaReadinessService';
import { type BetaGoal } from '../../../src/beta/BetaGoal';

describe('BetaReadinessService', () => {
  const goal: BetaGoal = {
    name: 'Test Beta',
    targetDate: '2026-01-01',
    successCriteria: ['x'],
    items: [
      {
        id: 'one',
        description: 'one desc',
        requiredPaths: ['a.ts', 'b.ts']
      },
      {
        id: 'two',
        description: 'two desc',
        requiredPaths: ['c.ts']
      }
    ]
  };

  it('reports full readiness when all paths exist', () => {
    const service = new BetaReadinessService({ exists: () => true }, goal);
    const report = service.evaluate();

    expect(report.readyForBeta).toBe(true);
    expect(report.progressPercent).toBe(100);
    expect(report.completed).toBe(2);
    expect(report.total).toBe(2);
  });

  it('reports missing blockers when paths are absent or probe throws', () => {
    const existing = new Set(['a.ts']);
    const service = new BetaReadinessService(
      {
        exists: (path: string) => {
          if (path === 'c.ts') {
            throw new Error('filesystem read failed');
          }
          return existing.has(path);
        }
      },
      goal
    );

    const report = service.evaluate();
    expect(report.readyForBeta).toBe(false);
    expect(report.progressPercent).toBe(0);

    const one = report.results.find((result) => result.id === 'one');
    const two = report.results.find((result) => result.id === 'two');

    expect(one?.missingPaths).toEqual(['b.ts']);
    expect(two?.missingPaths).toEqual(['c.ts']);
  });
});
