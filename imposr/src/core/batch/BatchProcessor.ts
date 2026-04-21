import { BatchProcessingError } from '@utils/errors';
import { JobQueue, type BatchJob } from './JobQueue';
import { WorkerPool } from './WorkerPool';

export interface BatchResult<TResult> {
  readonly processed: number;
  readonly results: TResult[];
}

/**
 * Orchestrates queue + worker pool for batch execution.
 */
export class BatchProcessor<TPayload, TResult> {
  constructor(
    private readonly queue: JobQueue<TPayload>,
    private readonly workerPool: WorkerPool
  ) {}

  /**
   * Adds a job to the processor queue.
   */
  public addJob(job: BatchJob<TPayload>): void {
    this.queue.enqueue(job);
  }

  /**
   * Processes all queued jobs.
   */
  public async processAll(task: (payload: TPayload) => Promise<TResult>): Promise<BatchResult<TResult>> {
    const jobs: BatchJob<TPayload>[] = [];
    let next = this.queue.dequeue();

    while (next) {
      jobs.push(next);
      next = this.queue.dequeue();
    }

    try {
      const results = await this.workerPool.runAll(
        jobs.map((job) => job.payload),
        task
      );

      return { processed: jobs.length, results };
    } catch (error) {
      throw new BatchProcessingError('Batch processing failed', jobs[0]?.id ?? 'unknown', {
        cause: (error as Error).message
      });
    }
  }
}
