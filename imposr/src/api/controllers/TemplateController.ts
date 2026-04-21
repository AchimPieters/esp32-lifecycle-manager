/**
 * Handles template API actions.
 */
export class TemplateController {
  public async list(): Promise<string[]> {
    return ['2up-a4-a3', 'booklet-16page'];
  }
}
