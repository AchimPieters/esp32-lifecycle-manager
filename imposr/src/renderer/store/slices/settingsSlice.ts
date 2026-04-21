export interface SettingsState {
  readonly darkMode: boolean;
}

export const initialSettingsState: SettingsState = { darkMode: false };

/** Updates dark mode setting. */
export function setDarkMode(state: SettingsState, darkMode: boolean): SettingsState {
  return { ...state, darkMode };
}
