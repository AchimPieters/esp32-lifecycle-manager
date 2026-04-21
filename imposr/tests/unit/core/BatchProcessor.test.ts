import { BatchProcessor } from '../../../src/core/batch/BatchProcessor';
import { JobQueue } from '../../../src/core/batch/JobQueue';
import { WorkerPool } from '../../../src/core/batch/WorkerPool';

describe('BatchProcessor', () => {
  it('processes all queued jobs', async () => {
    const queue = new JobQueue<number>();
    const workerPool = new WorkerPool(2);
    const processor = new BatchProcessor(queue, workerPool);

    processor.addJob({ id: 'job-1', payload: 2 });
    processor.addJob({ id: 'job-2', payload: 4 });

    const result = await processor.processAll(async (payload) => payload * 10);

    expect(result.processed).toBe(2);
    expect(result.results).toEqual([20, 40]);
  });

  it('wraps worker errors with BatchProcessingError', async () => {
    const queue = new JobQueue<number>();
    const workerPool = new WorkerPool(2);
    const processor = new BatchProcessor(queue, workerPool);

    processor.addJob({ id: 'job-1', payload: 2 });

    await expect(
      processor.processAll(async () => {
        throw new Error('network failure');
      })
    ).rejects.toThrow('Batch processing failed');
  });
});
