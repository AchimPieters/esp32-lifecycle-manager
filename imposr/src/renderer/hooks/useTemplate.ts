import { useState } from 'react';

/** Manages selected template id state. */
export function useTemplate(): { templateId: string; setTemplateId: (id: string) => void } {
  const [templateId, setTemplateIdState] = useState('');

  const setTemplateId = (id: string): void => {
    if (!id.trim()) {
      throw new Error('Template id is required');
    }

    setTemplateIdState(id);
  };

  return { templateId, setTemplateId };
}
