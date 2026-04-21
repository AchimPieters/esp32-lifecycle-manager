export interface JobProgressProps {
  readonly processed: number;
  readonly total: number;
}

/** Visualizes batch progress percentage for the active run. */
export function JobProgress({ processed, total }: JobProgressProps): JSX.Element {
  if (total <= 0 || processed < 0 || processed > total) {
    throw new Error('Invalid progress values');
  }

  const percent = Math.round((processed / total) * 100);
  return <progress max={100} value={percent} aria-label={`progress-${percent}`} />;
}
