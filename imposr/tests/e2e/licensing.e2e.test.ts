import { FeatureGate } from '../../src/licensing/FeatureGate';

describe('e2e: licensing gate', () => {
  it('allows enterprise-only features on enterprise tier', () => {
    const gate = new FeatureGate();
    expect(() => gate.assertFeature('enterprise-export', 'enterprise', 'enterprise')).not.toThrow();
  });
});
