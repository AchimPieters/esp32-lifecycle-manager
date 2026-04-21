export interface TelemetryEvent {
  readonly name: string;
  readonly properties?: Record<string, unknown>;
}

/**
 * Collects telemetry events in-memory.
 */
export class TelemetryCollector {
  private readonly events: TelemetryEvent[] = [];

  /**
   * Stores telemetry event.
   */
  public collect(event: TelemetryEvent): void {
    if (!event.name.trim()) {
      throw new Error('Telemetry event name is required');
    }

    this.events.push(event);
  }

  /**
   * Returns captured events snapshot.
   */
  public flush(): TelemetryEvent[] {
    return [...this.events];
  }
}
