import { ImposrError } from '@utils/errors';

export interface AcrobatBridgeConfig {
  readonly pluginId: string;
  readonly pluginVersion: string;
  readonly minAcrobatVersion: string;
}

export interface ImpositionRequest {
  readonly inputPath: string;
  readonly templateId: string;
  readonly outputPath: string;
}

/**
 * Bridge abstraction for Adobe Acrobat SDK integration points.
 */
export class AcrobatSDKBridge {
  private initialized = false;

  constructor(private readonly config: AcrobatBridgeConfig) {}

  /**
   * Initializes plugin bridge and validates static config.
   */
  public initialize(): void {
    if (!this.config.pluginId.trim() || !this.config.pluginVersion.trim()) {
      throw new ImposrError('Invalid Acrobat bridge config', 'ACROBAT_BRIDGE_CONFIG_ERROR');
    }

    this.initialized = true;
  }

  /**
   * Registers menu hooks in Acrobat host process.
   */
  public registerMenu(): string {
    if (!this.initialized) {
      throw new ImposrError('Bridge is not initialized', 'ACROBAT_BRIDGE_STATE_ERROR');
    }

    return `menu:${this.config.pluginId}:registered`;
  }

  /**
   * Executes a single imposition request routed through plugin bridge.
   */
  public executeImposition(request: ImpositionRequest): string {
    if (!this.initialized) {
      throw new ImposrError('Bridge is not initialized', 'ACROBAT_BRIDGE_STATE_ERROR');
    }

    if (!request.inputPath.trim() || !request.outputPath.trim() || !request.templateId.trim()) {
      throw new ImposrError('Invalid imposition request payload', 'ACROBAT_REQUEST_ERROR');
    }

    return `imposed:${request.inputPath}:${request.templateId}:${request.outputPath}`;
  }

  /**
   * Stops plugin bridge operations.
   */
  public shutdown(): void {
    this.initialized = false;
  }

  /**
   * Returns runtime state for observability.
   */
  public isInitialized(): boolean {
    return this.initialized;
  }
}
