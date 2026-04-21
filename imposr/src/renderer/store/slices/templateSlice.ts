export interface TemplateState {
  readonly id: string;
}

export const initialTemplateState: TemplateState = { id: '' };

/** Updates template state id. */
export function setTemplate(state: TemplateState, id: string): TemplateState {
  if (!id.trim()) {
    throw new Error('Template id is required');
  }

  return { ...state, id };
}
