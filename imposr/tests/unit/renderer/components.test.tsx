import React from 'react';
import { fireEvent, render, screen } from '@testing-library/react';
import { App } from '../../../src/renderer/App';
import { Button } from '../../../src/renderer/components/common/Button';
import { Input } from '../../../src/renderer/components/common/Input';
import { Modal } from '../../../src/renderer/components/common/Modal';
import { Dropdown } from '../../../src/renderer/components/common/Dropdown';
import { Toast } from '../../../src/renderer/components/common/Toast';
import { LoadingSpinner } from '../../../src/renderer/components/common/LoadingSpinner';

describe('renderer components', () => {
  it('renders app shell and common components', () => {
    render(<App />);
    expect(screen.getByText('Imposr Pro')).toBeInTheDocument();
    expect(screen.getByText('Canvas')).toBeInTheDocument();
  });

  it('handles common component interactions', () => {
    const onClick = jest.fn();
    const onChange = jest.fn();
    const onSelect = jest.fn();

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
      </>
    );

    fireEvent.click(screen.getByText('Save'));
    fireEvent.change(screen.getByDisplayValue('a'), { target: { value: 'b' } });
    fireEvent.change(screen.getByDisplayValue('one'), { target: { value: 'two' } });

    expect(onClick).toHaveBeenCalled();
    expect(onChange).toHaveBeenCalledWith('b');
    expect(onSelect).toHaveBeenCalledWith('two');
    expect(screen.getByRole('dialog')).toBeInTheDocument();
    expect(screen.getByRole('status')).toHaveTextContent('Done');
    expect(screen.getByLabelText('loading')).toBeInTheDocument();
  });
});
