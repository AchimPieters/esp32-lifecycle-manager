export interface BatchJobListProps {
  readonly jobIds: readonly string[];
}

/** Shows queued jobs that will be processed in batch mode. */
export function BatchJobList({ jobIds }: BatchJobListProps): JSX.Element {
  return (
    <ol>
      {jobIds.map((jobId) => (
        <li key={jobId}>{jobId}</li>
      ))}
    </ol>
  );
}
