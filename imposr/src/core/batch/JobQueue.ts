import { BatchProcessingError } from '@utils/errors';

export interface BatchJob<TPayload = unknown> {
  readonly id: string;
  readonly payload: TPayload;
}

/**
 * FIFO queue for batch jobs.
 */
export class JobQueue<TPayload = unknown> {
  private readonly queue: BatchJob<TPayload>[] = [];

  /**
   * Enqueues a job.
   */
  public enqueue(job: BatchJob<TPayload>): void {
    if (!job.id.trim()) {
      throw new BatchProcessingError('Job id is required', 'unknown');
    }

    this.queue.push(job);
  }

  /**
   * Dequeues next job or null when empty.
   */
  public dequeue(): BatchJob<TPayload> | null {
    return this.queue.shift() ?? null;
  }

  /**
   * Current queue length.
   */
  public size(): number {
    return this.queue.length;
  }
}
