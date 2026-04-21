import { FeatureGate } from '../../../src/licensing/FeatureGate';
import { LicenseManager } from '../../../src/licensing/LicenseManager';
import { MachineId } from '../../../src/licensing/MachineId';
import { OfflineValidator } from '../../../src/licensing/OfflineValidator';
import { PaymentHandler } from '../../../src/licensing/PaymentHandler';
import { FeatureNotAvailableError } from '../../../src/utils/errors';

describe('licensing modules', () => {
  it('generates deterministic machine id and validates offline keys', () => {
    const machineId = new MachineId().generate('seed-1');
    const validator = new OfflineValidator();

    expect(machineId).toHaveLength(64);
    expect(validator.validate(`${machineId.slice(0, 8)}-license`, machineId.slice(0, 8))).toBe(true);
    expect(() => validator.validate('', 'abc')).toThrow();
  });

  it('validates license payload with license manager', () => {
    const manager = new LicenseManager();
    const machine = new MachineId().generate('device-A');

    const valid = manager.validate({ key: `${machine.slice(0, 8)}-foo`, machineSeed: 'device-A' });
    const invalid = manager.validate({ key: `INVALID-foo`, machineSeed: 'device-A' });

    expect(valid.valid).toBe(true);
    expect(invalid.valid).toBe(false);
  });

  it('enforces feature gate tiers and creates checkout', async () => {
    const gate = new FeatureGate();
    expect(() => gate.assertFeature('batch', 'pro', 'trial')).not.toThrow();
    expect(() => gate.assertFeature('advanced', 'trial', 'enterprise')).toThrow(FeatureNotAvailableError);

    await expect(new PaymentHandler().createCheckout('c1', 'plan-pro')).resolves.toEqual({
      checkoutId: 'c1-plan-pro-checkout'
    });
    await expect(new PaymentHandler().createCheckout('', 'x')).rejects.toThrow();
  });
});
