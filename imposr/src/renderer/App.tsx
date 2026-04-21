import { Header } from './components/layout/Header';
import { Sidebar } from './components/layout/Sidebar';
import { MainCanvas } from './components/layout/MainCanvas';
import { SettingsPanel } from './components/layout/SettingsPanel';
import { StatusBar } from './components/layout/StatusBar';

/**
 * Main renderer shell.
 */
export function App(): JSX.Element {
  return (
    <div>
      <Header title="Imposr Pro" />
      <Sidebar sections={['PDF', 'Templates', 'Batch']} />
      <MainCanvas />
      <SettingsPanel />
      <StatusBar message="Ready" />
    </div>
  );
}
