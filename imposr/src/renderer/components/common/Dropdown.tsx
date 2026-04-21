export interface DropdownProps {
  readonly options: string[];
  readonly onSelect: (value: string) => void;
}

/** Reusable dropdown component. */
export function Dropdown({ options, onSelect }: DropdownProps): JSX.Element {
  return (
    <select onChange={(event) => onSelect(event.target.value)}>
      {options.map((option) => (
        <option key={option}>{option}</option>
      ))}
    </select>
  );
}
