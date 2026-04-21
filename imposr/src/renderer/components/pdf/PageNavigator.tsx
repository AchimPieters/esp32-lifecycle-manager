export interface PageNavigatorProps {
  readonly currentPage: number;
  readonly totalPages: number;
  readonly onNavigate: (page: number) => void;
}

/** Handles next/previous navigation events with bounds protection. */
export function PageNavigator({ currentPage, totalPages, onNavigate }: PageNavigatorProps): JSX.Element {
  if (currentPage <= 0 || totalPages <= 0 || currentPage > totalPages) {
    throw new Error('Invalid currentPage/totalPages combination');
  }

  return (
    <nav aria-label="page-navigator">
      <button type="button" disabled={currentPage === 1} onClick={() => onNavigate(currentPage - 1)}>
        Vorige
      </button>
      <span>
        {currentPage}/{totalPages}
      </span>
      <button type="button" disabled={currentPage === totalPages} onClick={() => onNavigate(currentPage + 1)}>
        Volgende
      </button>
    </nav>
  );
}
