import { ErrorReporter } from './ErrorReporter';
import { TelemetryCollector, type TelemetryEvent } from './TelemetryCollector';

/**
 * Analytics facade for telemetry and error reporting.
 */
export class AnalyticsClient {
  constructor(
    private readonly collector = new TelemetryCollector(),
    private readonly reporter = new ErrorReporter()
  ) {}

  /**
   * Records usage event.
   */
  public track(event: TelemetryEvent): void {
    this.collector.collect(event);
  }

  /**
   * Reports runtime error.
   */
  public report(error: Error): { message: string } {
    return this.reporter.report(error);
  }

  /**
   * Exposes buffered telemetry events.
   */
  public dump(): TelemetryEvent[] {
    return this.collector.flush();
  }
}
