import { useState } from 'react';

/** Manages simple boolean settings state. */
export function useSettings(): { darkMode: boolean; setDarkMode: (enabled: boolean) => void } {
  const [darkMode, setDarkModeState] = useState(false);

  const setDarkMode = (enabled: boolean): void => {
    setDarkModeState(Boolean(enabled));
  };

  return { darkMode, setDarkMode };
}
