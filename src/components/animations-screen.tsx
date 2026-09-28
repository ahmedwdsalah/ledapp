import * as Haptics from 'expo-haptics';
import { router } from 'expo-router';
import * as SplashScreen from 'expo-splash-screen';
import { StatusBar } from 'expo-status-bar';
import { useMemo } from 'react';
import { FlatList, Pressable, ScrollView, StyleSheet, Text, TextInput, View, useWindowDimensions } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import { DisplayBadge } from '@/components/display-badge';
import { galleryItems, type GalleryCategory } from '@/constants/gallery-art';
import { usePreviewSelection } from '@/context/preview-selection';

const categories = ['Emblems', 'Motion', 'Characters', 'Symbols', 'All'] as const;

export default function AnimationsScreen() {
  const { width } = useWindowDimensions();
  const insets = useSafeAreaInsets();
  const { previewId, selectedId, setPreviewId, libraryQuery, setLibraryQuery, libraryCategory, setLibraryCategory } = usePreviewSelection();
  const cellWidth = (width - 62) / 2;
  const artSize = Math.min(cellWidth - 10, 180);
  const filtered = useMemo(() => galleryItems.filter((item) =>
    (libraryCategory === 'All' || item.category === libraryCategory) &&
    item.name.toLocaleLowerCase().includes(libraryQuery.trim().toLocaleLowerCase()),
  ), [libraryCategory, libraryQuery]);

  function preview(id: number) {
    setPreviewId(id);
    Haptics.selectionAsync().catch(() => {});
    router.navigate('/studio');
  }

  return (
    <FlatList
      style={styles.root}
      data={filtered}
      keyExtractor={(item) => String(item.id)}
      numColumns={2}
      columnWrapperStyle={styles.row}
      initialNumToRender={8}
      maxToRenderPerBatch={8}
      windowSize={5}
      keyboardShouldPersistTaps="handled"
      contentInsetAdjustmentBehavior="never"
      contentContainerStyle={{ paddingHorizontal: 24, paddingTop: insets.top + 15, paddingBottom: insets.bottom + 100 }}
      showsVerticalScrollIndicator={false}
      onLayout={() => SplashScreen.hideAsync()}
      ListHeaderComponent={
        <View>
          <StatusBar style="light" />
          <Text style={styles.heading}>Animations</Text>
          <View style={styles.search}>
            <TextInput
              value={libraryQuery}
              onChangeText={setLibraryQuery}
              placeholder="Search animations"
              placeholderTextColor="#77777D"
              selectionColor="#F05850"
              returnKeyType="search"
              accessibilityLabel="Search animations"
              style={styles.searchInput}
            />
            {!!libraryQuery && <Pressable accessibilityRole="button" accessibilityLabel="Clear search" onPress={() => setLibraryQuery('')} style={styles.clear}><Text style={styles.clearText}>×</Text></Pressable>}
          </View>
          <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={styles.categories} style={styles.categoryScroll}>
            {categories.map((category) => (
              <Pressable
                key={category}
                accessibilityRole="tab"
                accessibilityState={{ selected: libraryCategory === category }}
                onPress={() => {
                  if (libraryCategory === category) return;
                  setLibraryCategory(category as 'All' | GalleryCategory);
                  Haptics.selectionAsync().catch(() => {});
                }}
                style={[styles.category, libraryCategory === category && styles.categoryActive]}>
                <Text style={[styles.categoryText, libraryCategory === category && styles.categoryTextActive]}>{category}</Text>
              </Pressable>
            ))}
          </ScrollView>
          <Text style={styles.count}>{filtered.length} {filtered.length === 1 ? 'animation' : 'animations'}</Text>
        </View>
      }
      ListEmptyComponent={
        <View style={styles.empty}>
          <Text style={styles.emptyTitle}>No matches</Text>
          <Pressable accessibilityRole="button" onPress={() => { setLibraryQuery(''); setLibraryCategory('All'); }} style={styles.reset}><Text style={styles.resetText}>Show all animations</Text></Pressable>
        </View>
      }
      renderItem={({ item }) => (
        <Pressable
          accessibilityRole="button"
          accessibilityLabel={`Preview ${item.name}${selectedId === item.id ? ', chosen' : ''}`}
          onPress={() => preview(item.id)}
          style={({ pressed }) => [styles.item, { width: cellWidth }, pressed && styles.pressed]}>
          <View style={[styles.art, { width: artSize, height: artSize }]}>
            <DisplayBadge item={item} size={artSize} style={previewId === item.id ? styles.previewed : undefined} />
            {selectedId === item.id && <View style={styles.chosenDot} />}
          </View>
          <Text numberOfLines={1} style={styles.name}>{item.name}</Text>
        </Pressable>
      )}
    />
  );
}

const styles = StyleSheet.create({
  root: { flex: 1, backgroundColor: '#08090B' },
  heading: { color: '#F6F4F0', fontSize: 37, lineHeight: 45, fontWeight: '700', letterSpacing: -1.4, marginBottom: 20 },
  search: { flexDirection: 'row', alignItems: 'center', borderBottomWidth: 1, borderColor: '#393A3E', minHeight: 50 },
  searchInput: { flex: 1, color: '#F6F4F0', fontSize: 16, paddingVertical: 8, minHeight: 48 },
  clear: { width: 44, height: 44, alignItems: 'center', justifyContent: 'center' },
  clearText: { color: '#B5B5BB', fontSize: 26 },
  categoryScroll: { marginHorizontal: -24, marginTop: 20 },
  categories: { paddingHorizontal: 24, gap: 25 },
  category: { minHeight: 44, justifyContent: 'center', borderBottomWidth: 2, borderBottomColor: 'transparent' },
  categoryActive: { borderBottomColor: '#F05850' },
  categoryText: { color: '#85858B', fontSize: 14, fontWeight: '600' },
  categoryTextActive: { color: '#F6F4F0' },
  count: { color: '#85858B', fontSize: 12, marginTop: 22, marginBottom: 20, fontVariant: ['tabular-nums'] },
  row: { justifyContent: 'space-between' },
  item: { alignItems: 'center', marginBottom: 30, minHeight: 185 },
  pressed: { opacity: 0.74 },
  art: { justifyContent: 'center', alignItems: 'center' },
  previewed: { borderWidth: 2, borderColor: '#EDEBE7' },
  chosenDot: { position: 'absolute', top: 4, right: 4, width: 12, height: 12, borderRadius: 6, backgroundColor: '#F05850', borderWidth: 2, borderColor: '#08090B' },
  name: { color: '#DDDCE0', fontSize: 14, fontWeight: '600', marginTop: 10, maxWidth: '100%', textAlign: 'center' },
  empty: { alignItems: 'center', paddingTop: 90, gap: 17 },
  emptyTitle: { color: '#E5E3E0', fontSize: 19, fontWeight: '600' },
  reset: { minHeight: 44, justifyContent: 'center', paddingHorizontal: 14 },
  resetText: { color: '#F05850', fontSize: 15, fontWeight: '700' },
});
