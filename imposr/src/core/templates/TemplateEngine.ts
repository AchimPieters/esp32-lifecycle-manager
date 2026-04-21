import { ImpositionEngine, type ImpositionPlan } from '@core/imposition/ImpositionEngine';
import { TemplateLibrary } from './TemplateLibrary';

/**
 * Applies stored templates to document-level page counts.
 */
export class TemplateEngine {
  constructor(
    private readonly library: TemplateLibrary,
    private readonly engine: ImpositionEngine = new ImpositionEngine()
  ) {}

  /**
   * Builds an imposition plan from template id and page count.
   */
  public apply(templateId: string, pageCount: number): ImpositionPlan {
    const template = this.library.get(templateId);
    return this.engine.createPlan(pageCount, template.mode, template.sheet);
  }
}
