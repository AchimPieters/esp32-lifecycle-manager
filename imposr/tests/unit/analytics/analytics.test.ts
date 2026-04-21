import { AnalyticsClient } from '../../../src/analytics/AnalyticsClient';
import { ErrorReporter } from '../../../src/analytics/ErrorReporter';
import { TelemetryCollector } from '../../../src/analytics/TelemetryCollector';

describe('analytics modules', () => {
  it('collects telemetry events', () => {
    const collector = new TelemetryCollector();
    collector.collect({ name: 'app_open' });

    expect(collector.flush()).toEqual([{ name: 'app_open' }]);
    expect(() => collector.collect({ name: '' })).toThrow();
  });

  it('reports errors', () => {
    const reporter = new ErrorReporter();
    expect(reporter.report(new Error('boom'))).toEqual({ message: 'boom' });
    expect(() => reporter.report(new Error(''))).toThrow();
  });

  it('provides analytics facade', () => {
    const client = new AnalyticsClient();
    client.track({ name: 'render', properties: { ms: 12 } });

    expect(client.dump()).toHaveLength(1);
    expect(client.report(new Error('e'))).toEqual({ message: 'e' });
  });
});
