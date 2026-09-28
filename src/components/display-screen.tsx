import * as Haptics from 'expo-haptics';
import { Image } from 'expo-image';
import * as SplashScreen from 'expo-splash-screen';
import { StatusBar } from 'expo-status-bar';
import { useCallback, useEffect, useRef } from 'react';
import { FlatList, Pressable, ScrollView, StyleSheet, Text, View, useWindowDimensions } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import { DisplayBadge } from '@/components/display-badge';
import { galleryItems, type GalleryItem } from '@/constants/gallery-art';
import { usePreviewSelection } from '@/context/preview-selection';
import { useDynamicNotifications } from '@/hooks/use-dynamic-notifications';

const INK = '#08090B';
const CORAL = '#F05850';
let didShowNotificationPreview = false;

export default function DisplayScreen() {
  const { width } = useWindowDimensions();
  const insets = useSafeAreaInsets();
  const { previewId, setPreviewId, selectedId, setSelectedId } = usePreviewSelection();
  const { trigger } = useDynamicNotifications();
  const carousel = useRef<FlatList<GalleryItem>>(null);
  const rail = useRef<FlatList<GalleryItem>>(null);
  const currentId = useRef(previewId);
  const pageWidth = Math.round(width * 0.82);
  const badgeSize = Math.min(Math.round(width * 0.74), 344);
  const stageHeight = badgeSize + 60;
  const current = galleryItems[previewId];
  const isChosen = selectedId === previewId;

  const notify = useCallback((item: GalleryItem, title: string) => {
    trigger({
      id: `motif-${item.id}`,
      title,
      message: item.name,
      render: () => (
        <View style={styles.notificationBody}>
          <DisplayBadge item={item} size={46} />
          <View style={styles.notificationCopy}>
            <Text numberOfLines={1} style={styles.notificationTitle}>{title}</Text>
            <Text numberOfLines={1} style={styles.notificationMessage}>{item.name}</Text>
          </View>
        </View>
      ),
    });
  }, [trigger]);

  useEffect(() => {
    if (didShowNotificationPreview) return;
    const timer = setTimeout(() => {
      didShowNotificationPreview = true;
      notify(galleryItems[currentId.current], 'Preview ready');
    }, 1200);
    return () => clearTimeout(timer);
  }, [notify]);

  useEffect(() => {
    if (currentId.current === previewId) return;
    currentId.current = previewId;
    carousel.current?.scrollToIndex({ index: previewId, animated: false });
    rail.current?.scrollToIndex({ index: previewId, animated: true, viewPosition: 0.5 });
  }, [previewId]);

  function show(id: number) {
    if (id === currentId.current) return;
    currentId.current = id;
    setPreviewId(id);
    carousel.current?.scrollToIndex({ index: id, animated: true });
    rail.current?.scrollToIndex({ index: id, animated: true, viewPosition: 0.5 });
    Haptics.selectionAsync().catch(() => {});
  }

  function choose() {
    if (isChosen) return;
    setSelectedId(previewId);
    Haptics.notificationAsync(Haptics.NotificationFeedbackType.Success).catch(() => {});
    notify(current, 'Chosen for display');
  }

  return (
    <ScrollView
      style={styles.root}
      contentContainerStyle={{ paddingTop: insets.top + 18, paddingBottom: insets.bottom + 100 }}
      contentInsetAdjustmentBehavior="never"
      showsVerticalScrollIndicator={false}
      onLayout={() => SplashScreen.hideAsync()}>
      <StatusBar style="light" />
      <View style={styles.header}>
        <Pressable accessibilityRole="button" accessibilityLabel="Replay notification preview" onPress={() => notify(galleryItems[currentId.current], 'Preview ready')} style={styles.identity}>
          <Image source={require('../../assets/images/motif-mark.png')} contentFit="contain" style={styles.mark} />
          <Text style={styles.wordmark}>MOTIF</Text>
        </Pressable>
        <Text style={styles.counter}>{String(previewId + 1).padStart(2, '0')} <Text style={styles.counterMuted}>/ {galleryItems.length}</Text></Text>
      </View>

      <View style={styles.stage}>
        <View pointerEvents="none" style={styles.paintPlane} />
        <View pointerEvents="none" style={styles.paintSeam} />
        <FlatList
          ref={carousel}
          data={galleryItems}
          keyExtractor={(item) => String(item.id)}
          horizontal
          snapToInterval={pageWidth}
          decelerationRate="fast"
          bounces={false}
          initialScrollIndex={previewId}
          getItemLayout={(_, index) => ({ length: pageWidth, offset: pageWidth * index, index })}
          contentContainerStyle={{ paddingHorizontal: (width - pageWidth) / 2 }}
          initialNumToRender={3}
          maxToRenderPerBatch={3}
          windowSize={3}
          showsHorizontalScrollIndicator={false}
          accessibilityLabel="Swipe to preview animations"
          onMomentumScrollEnd={(event) => {
            const id = Math.max(0, Math.min(galleryItems.length - 1, Math.round(event.nativeEvent.contentOffset.x / pageWidth)));
            if (id === currentId.current) return;
            currentId.current = id;
            setPreviewId(id);
            rail.current?.scrollToIndex({ index: id, animated: true, viewPosition: 0.5 });
            Haptics.selectionAsync().catch(() => {});
          }}
          renderItem={({ item }) => (
            <View style={[styles.page, { width: pageWidth, height: stageHeight }]}>
              <View style={[styles.deviceShadow, { width: badgeSize, height: badgeSize, borderRadius: badgeSize / 2 }]}>
                <DisplayBadge item={item} size={badgeSize} />
              </View>
            </View>
          )}
          style={{ height: stageHeight }}
        />
      </View>

      <View style={styles.details}>
        <Text style={styles.eyebrow}>EMBLEM PREVIEW <Text style={styles.separator}>·</Text> {current.category.toUpperCase()}</Text>
        <Text style={styles.title} numberOfLines={2}>{current.name}</Text>
      </View>

      <FlatList
        ref={rail}
        data={galleryItems}
        keyExtractor={(item) => String(item.id)}
        horizontal
        initialScrollIndex={Math.max(0, previewId - 2)}
        getItemLayout={(_, index) => ({ length: 68, offset: 68 * index, index })}
        initialNumToRender={8}
        maxToRenderPerBatch={8}
        windowSize={5}
        showsHorizontalScrollIndicator={false}
        contentContainerStyle={styles.rail}
        renderItem={({ item }) => (
          <Pressable
            accessibilityRole="button"
            accessibilityLabel={`Preview ${item.name}`}
            accessibilityState={{ selected: item.id === previewId }}
            onPress={() => show(item.id)}
            style={styles.railTarget}>
            <DisplayBadge item={item} size={item.id === previewId ? 56 : 48} style={item.id === previewId ? styles.railCurrent : styles.railOther} />
            {item.id === selectedId && <View style={styles.railChosen} />}
            {item.id === previewId && <View style={styles.railIndicator} />}
          </Pressable>
        )}
      />

      <View style={styles.actionArea}>
        <Pressable
          accessibilityRole="button"
          accessibilityState={{ selected: isChosen }}
          onPress={choose}
          style={({ pressed }) => [styles.action, isChosen && styles.actionChosen, pressed && !isChosen && styles.actionPressed]}>
          <Text style={[styles.actionText, isChosen && styles.actionTextChosen]}>{isChosen ? 'Chosen for display' : 'Choose this animation'}</Text>
          <Text style={[styles.actionIcon, isChosen && styles.actionTextChosen]}>{isChosen ? '✓' : '↗'}</Text>
        </Pressable>
      </View>
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  root: { flex: 1, backgroundColor: INK },
  header: { paddingHorizontal: 25, flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between' },
  identity: { flexDirection: 'row', alignItems: 'center', gap: 10 },
  mark: { width: 25, height: 25 },
  wordmark: { color: '#F7F5F1', fontSize: 15, fontWeight: '800', letterSpacing: 3.4 },
  counter: { color: '#F2F0EC', fontSize: 14, fontWeight: '700', fontVariant: ['tabular-nums'] },
  counterMuted: { color: '#77777B', fontWeight: '500' },
  stage: { marginTop: 16, justifyContent: 'center', overflow: 'hidden' },
  paintPlane: { position: 'absolute', left: 0, right: 0, top: '28%', bottom: '21%', backgroundColor: '#15171B', experimental_backgroundImage: 'linear-gradient(180deg, #22242A 0%, #101114 18%, #0A0B0E 68%, #201112 100%)' },
  paintSeam: { position: 'absolute', left: 0, right: 0, top: '28%', height: 1, backgroundColor: 'rgba(255,255,255,0.09)' },
  page: { alignItems: 'center', justifyContent: 'center' },
  deviceShadow: { backgroundColor: '#000', boxShadow: '0 20px 48px rgba(0,0,0,0.75)' },
  details: { paddingHorizontal: 26, alignItems: 'center', marginTop: 5, minHeight: 70 },
  eyebrow: { color: CORAL, fontSize: 10, fontWeight: '800', letterSpacing: 1.8 },
  separator: { color: '#65656A' },
  title: { color: '#F6F4F0', fontSize: 31, lineHeight: 39, fontWeight: '700', letterSpacing: -1.2, textAlign: 'center', marginTop: 7 },
  rail: { paddingHorizontal: 18, paddingTop: 5, paddingBottom: 7 },
  railTarget: { width: 68, height: 64, alignItems: 'center', justifyContent: 'center' },
  railCurrent: { borderWidth: 2, borderColor: '#F2F0EC' },
  railOther: { opacity: 0.66 },
  railChosen: { position: 'absolute', top: 7, right: 6, width: 9, height: 9, borderRadius: 5, backgroundColor: CORAL, borderWidth: 1, borderColor: INK },
  railIndicator: { position: 'absolute', bottom: 0, width: 11, height: 2, borderRadius: 1, backgroundColor: CORAL },
  actionArea: { paddingHorizontal: 25, paddingTop: 13 },
  action: { minHeight: 58, paddingHorizontal: 21, borderRadius: 18, backgroundColor: '#F2F0EC', flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between' },
  actionChosen: { backgroundColor: '#281617', borderWidth: 1, borderColor: CORAL },
  actionPressed: { opacity: 0.84, transform: [{ scale: 0.985 }] },
  actionText: { color: '#111214', fontSize: 16, fontWeight: '700' },
  actionTextChosen: { color: '#F4EAE8' },
  actionIcon: { color: '#111214', fontSize: 23, fontWeight: '500' },
  notificationBody: { flex: 1, flexDirection: 'row', alignItems: 'center', paddingLeft: 13, paddingRight: 20, gap: 12 },
  notificationCopy: { flex: 1 },
  notificationTitle: { color: CORAL, fontSize: 17, fontWeight: '700', letterSpacing: -0.35 },
  notificationMessage: { color: '#77787D', fontSize: 14, fontWeight: '500', letterSpacing: -0.2, marginTop: 1 },
});
