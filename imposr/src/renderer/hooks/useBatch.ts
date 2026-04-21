import { useState } from 'react';

/** Manages batch queue length state. */
export function useBatch(): { jobs: number; setJobs: (jobs: number) => void } {
  const [jobs, setJobsState] = useState(0);

  const setJobs = (value: number): void => {
    if (value < 0) {
      throw new Error('Jobs cannot be negative');
    }

    setJobsState(value);
  };

  return { jobs, setJobs };
}
