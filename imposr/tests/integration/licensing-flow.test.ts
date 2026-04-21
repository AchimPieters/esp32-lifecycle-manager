import { LicenseManager } from '../../src/licensing/LicenseManager';
import { MachineId } from '../../src/licensing/MachineId';

describe('integration: licensing flow', () => {
  it('validates bound machine license', () => {
    const seed = 'workstation-01';
    const machine = new MachineId().generate(seed);
    const key = `${machine.slice(0, 8)}-offline`;

    const result = new LicenseManager().validate({ key, machineSeed: seed });

    expect(result.valid).toBe(true);
    expect(result.machineId).toBe(machine);
  });
});
