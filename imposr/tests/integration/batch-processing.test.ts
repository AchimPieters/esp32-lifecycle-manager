import { BatchProcessor } from '../../src/core/batch/BatchProcessor';
import { JobQueue } from '../../src/core/batch/JobQueue';
import { WorkerPool } from '../../src/core/batch/WorkerPool';

describe('integration: batch processing', () => {
  it('runs batch end-to-end', async () => {
    const queue = new JobQueue<number>();
    const processor = new BatchProcessor<number, number>(queue, new WorkerPool(2));

    processor.addJob({ id: 'a', payload: 2 });
    processor.addJob({ id: 'b', payload: 3 });

    const result = await processor.processAll(async (payload) => payload * payload);

    expect(result.processed).toBe(2);
    expect(result.results).toEqual([4, 9]);
  });
});
