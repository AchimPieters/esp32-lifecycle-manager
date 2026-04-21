import { BatchProcessingError } from '@utils/errors';

export type WorkerTask<TPayload, TResult> = (payload: TPayload) => Promise<TResult>;

/**
 * Executes tasks with bounded concurrency.
 */
export class WorkerPool {
  constructor(private readonly concurrency: number) {
    if (concurrency <= 0) {
      throw new BatchProcessingError('Concurrency must be greater than zero', 'worker-pool');
    }
  }

  /**
   * Runs tasks in chunks honoring configured concurrency.
   */
  public async runAll<TPayload, TResult>(
    payloads: TPayload[],
    task: WorkerTask<TPayload, TResult>
  ): Promise<TResult[]> {
    const results: TResult[] = [];

    for (let index = 0; index < payloads.length; index += this.concurrency) {
      const slice = payloads.slice(index, index + this.concurrency);
      const chunk = await Promise.all(slice.map((payload) => task(payload)));
      results.push(...chunk);
    }

    return results;
  }
}
