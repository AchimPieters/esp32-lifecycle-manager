import { BatchProcessor } from '../../../src/core/batch/BatchProcessor';
import { JobQueue } from '../../../src/core/batch/JobQueue';
import { WorkerPool } from '../../../src/core/batch/WorkerPool';
import { BatchProcessingError } from '../../../src/utils/errors';

describe('batch core modules', () => {
  it('enqueues and dequeues jobs', () => {
    const queue = new JobQueue<number>();
    queue.enqueue({ id: 'job-1', payload: 1 });

    expect(queue.size()).toBe(1);
    expect(queue.dequeue()).toEqual({ id: 'job-1', payload: 1 });
    expect(queue.dequeue()).toBeNull();
  });

  it('processes queued jobs', async () => {
    const queue = new JobQueue<number>();
    const pool = new WorkerPool(2);
    const processor = new BatchProcessor<number, number>(queue, pool);

    processor.addJob({ id: 'a', payload: 1 });
    processor.addJob({ id: 'b', payload: 2 });

    const result = await processor.processAll(async (payload) => payload * 2);

    expect(result.processed).toBe(2);
    expect(result.results).toEqual([2, 4]);
  });

  it('throws on invalid queue/pool input', async () => {
    const queue = new JobQueue<number>();
    expect(() => queue.enqueue({ id: '', payload: 1 })).toThrow(BatchProcessingError);
    expect(() => new WorkerPool(0)).toThrow(BatchProcessingError);
  });
});
