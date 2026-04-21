/** @jest-environment jsdom */
import { act, fireEvent, render, screen } from '@testing-library/react';
import { App } from '../../../src/renderer/App';
import { Button } from '../../../src/renderer/components/common/Button';
import { Input } from '../../../src/renderer/components/common/Input';
import { Modal } from '../../../src/renderer/components/common/Modal';
import { Dropdown } from '../../../src/renderer/components/common/Dropdown';
import { Toast } from '../../../src/renderer/components/common/Toast';
import { LoadingSpinner } from '../../../src/renderer/components/common/LoadingSpinner';
import { PDFViewer } from '../../../src/renderer/components/pdf/PDFViewer';
import { PDFThumbnail } from '../../../src/renderer/components/pdf/PDFThumbnail';
import { PDFRenderer } from '../../../src/renderer/components/pdf/PDFRenderer';
import { PageNavigator } from '../../../src/renderer/components/pdf/PageNavigator';
import { TemplateSelector } from '../../../src/renderer/components/templates/TemplateSelector';
import { TemplatePreview } from '../../../src/renderer/components/templates/TemplatePreview';
import { TemplateEditor } from '../../../src/renderer/components/templates/TemplateEditor';
import { TemplateLibrary } from '../../../src/renderer/components/templates/TemplateLibrary';
import { BatchJobList } from '../../../src/renderer/components/batch/BatchJobList';
import { JobProgress } from '../../../src/renderer/components/batch/JobProgress';
import { BatchSettings } from '../../../src/renderer/components/batch/BatchSettings';
import { ActivationForm } from '../../../src/renderer/components/licensing/ActivationForm';
import { LicenseDialog } from '../../../src/renderer/components/licensing/LicenseDialog';
import { UpgradePrompt } from '../../../src/renderer/components/licensing/UpgradePrompt';

describe('renderer components', () => {
  it('renders app shell and common components', () => {
    render(<App />);
    expect(screen.getByText('Imposr Pro')).toBeTruthy();
    expect(screen.getByText('Canvas')).toBeTruthy();
  });

  it('handles common and beta component interactions', async () => {
    const onClick = jest.fn();
    const onChange = jest.fn();
    const onSelect = jest.fn();
    const onNavigate = jest.fn();
    const onSave = jest.fn();
    const onConcurrencyChange = jest.fn();
    const onActivate = jest.fn<Promise<void>, [string]>().mockResolvedValue(undefined);
    const onUpgrade = jest.fn<Promise<void>, []>().mockResolvedValue(undefined);
    const onClose = jest.fn();

    render(
      <>
        <Button label="Save" onClick={onClick} />
        <Input value="a" onChange={onChange} />
        <Modal open>
          <span>Open</span>
        </Modal>
        <Dropdown options={['one', 'two']} onSelect={onSelect} />
        <Toast message="Done" />
        <LoadingSpinner />
        <PDFViewer title="Doc" pageCount={4} />
        <PDFThumbnail pageNumber={1} />
        <PDFRenderer activePage={1} />
        <PageNavigator currentPage={2} totalPages={5} onNavigate={onNavigate} />
        <TemplateSelector templateIds={['a', 'b']} selectedTemplate="a" onSelect={onSelect} />
        <TemplatePreview templateId="a" description="desc" />
        <TemplateEditor initialName="A" onSave={onSave} />
        <TemplateLibrary templates={['a', 'b']} />
        <BatchJobList jobIds={['job-1']} />
        <JobProgress processed={1} total={2} />
        <BatchSettings concurrency={2} onConcurrencyChange={onConcurrencyChange} />
        <ActivationForm onActivate={onActivate} />
        <LicenseDialog
          open
          currentTier="pro"
          onActivate={onActivate}
          onUpgrade={onUpgrade}
          onClose={onClose}
        />
        <UpgradePrompt currentTier="pro" targetTier="enterprise" onUpgrade={onUpgrade} />
      </>
    );

    fireEvent.click(screen.getByText('Save'));
    const aInputs = screen.getAllByDisplayValue('a');
    fireEvent.change(aInputs[0], { target: { value: 'b' } });
    fireEvent.change(aInputs[1], { target: { value: 'two' } });
    fireEvent.click(screen.getByText('Vorige'));
    fireEvent.click(screen.getByText('Opslaan'));
    fireEvent.change(screen.getByDisplayValue('2'), { target: { value: '3' } });
    fireEvent.change(screen.getByLabelText('Licentiesleutel'), { target: { value: 'KEY-1' } });
    await act(async () => {
      fireEvent.submit(screen.getAllByLabelText('license-activation-form')[0]);
    });
    fireEvent.click(screen.getAllByRole('button', { name: 'Upgrade nu' })[0]);

    expect(onClick).toHaveBeenCalled();
    expect(onChange).toHaveBeenCalledWith('b');
    expect(onSelect).toHaveBeenCalled();
    expect(onNavigate).toHaveBeenCalledWith(1);
    expect(onSave).toHaveBeenCalledWith('A');
    expect(onConcurrencyChange).toHaveBeenCalledWith(3);
    expect(onActivate).toHaveBeenCalled();
    expect(onUpgrade).toHaveBeenCalled();
    expect(screen.getByText('Pagina 1')).toBeTruthy();
    expect(screen.getByText('Rendering pagina 1')).toBeTruthy();
  });

  it('handles enterprise tier dialog rendering', () => {
    const onActivate = jest.fn<Promise<void>, [string]>().mockResolvedValue(undefined);
    const onUpgrade = jest.fn<Promise<void>, []>().mockResolvedValue(undefined);

    render(
      <LicenseDialog
        open
        currentTier="enterprise"
        onActivate={onActivate}
        onUpgrade={onUpgrade}
        onClose={() => undefined}
      />
    );

    expect(screen.queryByText('Upgrade je abonnement')).toBeNull();
  });
});
