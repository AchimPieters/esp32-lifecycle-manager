import { AcrobatSDKBridge, type ImpositionRequest } from './AcrobatSDKBridge';

/**
 * Coordinates plugin startup/shutdown lifecycle for Acrobat runtime.
 */
export class PluginLifecycleManager {
  constructor(private readonly bridge: AcrobatSDKBridge) {}

  /**
   * Starts plugin runtime.
   */
  public start(): string {
    this.bridge.initialize();
    return this.bridge.registerMenu();
  }

  /**
   * Executes plugin imposition transaction.
   */
  public process(request: ImpositionRequest): string {
    return this.bridge.executeImposition(request);
  }

  /**
   * Stops plugin runtime.
   */
  public stop(): void {
    this.bridge.shutdown();
  }
}
