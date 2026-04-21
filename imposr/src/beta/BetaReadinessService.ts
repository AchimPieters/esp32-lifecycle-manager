import { betaGoal, type BetaGoal, type BetaGoalItem } from './BetaGoal';

export interface PathProbe {
  readonly exists: (path: string) => boolean;
}

export interface BetaReadinessItemResult {
  readonly id: string;
  readonly description: string;
  readonly complete: boolean;
  readonly missingPaths: readonly string[];
}

export interface BetaReadinessReport {
  readonly goalName: string;
  readonly targetDate: string;
  readonly completed: number;
  readonly total: number;
  readonly progressPercent: number;
  readonly readyForBeta: boolean;
  readonly results: readonly BetaReadinessItemResult[];
}

/**
 * Evaluates repository readiness against the provisional commercial beta goal.
 */
export class BetaReadinessService {
  constructor(
    private readonly pathProbe: PathProbe,
    private readonly goal: BetaGoal = betaGoal
  ) {}

  /**
   * Builds a detailed readiness report with completion metrics and blockers.
   */
  public evaluate(): BetaReadinessReport {
    const results = this.goal.items.map((item) => this.evaluateItem(item));
    const completed = results.filter((result) => result.complete).length;
    const total = results.length;

    return {
      goalName: this.goal.name,
      targetDate: this.goal.targetDate,
      completed,
      total,
      progressPercent: Math.round((completed / total) * 100),
      readyForBeta: completed === total,
      results
    };
  }

  private evaluateItem(item: BetaGoalItem): BetaReadinessItemResult {
    const missingPaths = item.requiredPaths.filter((path) => {
      try {
        return !this.pathProbe.exists(path);
      } catch {
        return true;
      }
    });

    return {
      id: item.id,
      description: item.description,
      complete: missingPaths.length === 0,
      missingPaths
    };
  }
}
