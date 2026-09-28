import { createContext, use, useState, type PropsWithChildren } from 'react';

import type { GalleryCategory } from '@/constants/gallery-art';

type PreviewSelection = {
  previewId: number;
  setPreviewId: (id: number) => void;
  selectedId: number | null;
  setSelectedId: (id: number) => void;
  libraryQuery: string;
  setLibraryQuery: (query: string) => void;
  libraryCategory: 'All' | GalleryCategory;
  setLibraryCategory: (category: 'All' | GalleryCategory) => void;
};

const PreviewSelectionContext = createContext<PreviewSelection | null>(null);

export function PreviewSelectionProvider({ children }: PropsWithChildren) {
  const [previewId, setPreviewId] = useState(0);
  const [selectedId, setSelectedId] = useState<number | null>(null);
  const [libraryQuery, setLibraryQuery] = useState('');
  const [libraryCategory, setLibraryCategory] = useState<'All' | GalleryCategory>('Emblems');
  return (
    <PreviewSelectionContext.Provider value={{ previewId, setPreviewId, selectedId, setSelectedId, libraryQuery, setLibraryQuery, libraryCategory, setLibraryCategory }}>
      {children}
    </PreviewSelectionContext.Provider>
  );
}

export function usePreviewSelection() {
  const value = use(PreviewSelectionContext);
  if (!value) throw new Error('PreviewSelectionProvider is missing');
  return value;
}
