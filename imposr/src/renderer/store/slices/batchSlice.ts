export interface BatchState {
  readonly jobs: number;
}

export const initialBatchState: BatchState = { jobs: 0 };

/** Updates batch job count. */
export function setBatchJobs(state: BatchState, jobs: number): BatchState {
  if (jobs < 0) {
    throw new Error('Jobs cannot be negative');
  }

  return { ...state, jobs };
}
