import { MachineId } from './MachineId';
import { OfflineValidator } from './OfflineValidator';

export interface LicensePayload {
  readonly key: string;
  readonly machineSeed: string;
}

/**
 * Coordinates machine binding and offline key validation.
 */
export class LicenseManager {
  constructor(
    private readonly machineId = new MachineId(),
    private readonly offlineValidator = new OfflineValidator()
  ) {}

  /**
   * Validates a license key against machine id.
   */
  public validate(payload: LicensePayload): { valid: boolean; machineId: string } {
    const machine = this.machineId.generate(payload.machineSeed);
    const valid = this.offlineValidator.validate(payload.key, machine.slice(0, 8));

    return { valid, machineId: machine };
  }
}
