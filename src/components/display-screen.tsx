import * as Haptics from 'expo-haptics';
import { BlurTargetView, BlurView } from 'expo-blur';
import { Image } from 'expo-image';
import * as SplashScreen from 'expo-splash-screen';
import { StatusBar } from 'expo-status-bar';
import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { FlatList, Pressable, StyleSheet, Text, View, useWindowDimensions } from 'react-native';
import { Gesture, GestureDetector } from 'react-native-gesture-handler';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import Animated, { FadeIn, FadeOut } from 'react-native-reanimated';
import { scheduleOnRN } from 'react-native-worklets';

import { DisplayBadge } from '@/components/display-badge';
import { ConnectDisplay } from '@/device/connect-display';
import { savedDisplay, uploadAnimation } from '@/device/motif-device';
import { galleryItems, type GalleryItem } from '@/constants/gallery-art';
import { usePreviewSelection } from '@/context/preview-selection';
import { useDynamicNotifications } from '@/hooks/use-dynamic-notifications';

type HomeSection = 'My Library' | 'Individuals' | 'Packs';
const sections: HomeSection[] = ['My Library', 'Individuals', 'Packs'];
const INK = '#08090B';
const CORAL = '#F05850';
let didShowNotificationPreview = false;

export default function DisplayScreen() {
  const { width } = useWindowDimensions();
  const insets = useSafeAreaInsets();
  const { previewId, setPreviewId, selectedId, setSelectedId } = usePreviewSelection();
  const { trigger } = useDynamicNotifications();
  const list = useRef<FlatList<GalleryItem>>(null);
  const blurTarget = useRef<View | null>(null);
  const currentId = useRef(previewId);
  const [section, setSection] = useState<HomeSection>('Packs');
  const [connectOpen, setConnectOpen] = useState(false);
  const [uploading, setUploading] = useState(false);
  const heroWidth = width - 32;
  const heroSize = Math.min(heroWidth * 0.78, 320);
  const heroHeight = heroWidth;
  const cellWidth = (width - 64) / 3;
  const current = galleryItems[previewId];
  const heroImageSize = current.framed ? heroSize * 1.42 : heroSize * 0.86;

  const visibleItems = useMemo(() => {
    if (section === 'My Library') return selectedId === null ? [] : [galleryItems[selectedId]];
    if (section === 'Packs') return galleryItems.slice(0, 13);
    return galleryItems;
  }, [section, selectedId]);

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
    currentId.current = previewId;
  }, [previewId]);

  const show = useCallback((id: number, scrollToTop = false) => {
    if (id === currentId.current && !scrollToTop) return;
    currentId.current = id;
    setPreviewId(id);
    setSelectedId(id);
    if (scrollToTop) list.current?.scrollToOffset({ offset: 0, animated: true });
    Haptics.selectionAsync().catch(() => {});
  }, [setPreviewId, setSelectedId]);

  const step = useCallback((direction: number) => {
    show((currentId.current + direction + galleryItems.length) % galleryItems.length);
  }, [show]);

  const swipeGesture = Gesture.Pan()
    .activeOffsetX([-14, 14])
    .failOffsetY([-18, 18])
    .onEnd((event) => {
      const direction = Math.abs(event.velocityX) > 420 ? event.velocityX : event.translationX;
      if (Math.abs(event.translationX) > 42 || Math.abs(event.velocityX) > 420) {
        scheduleOnRN(step, direction < 0 ? 1 : -1);
      }
    });

  function changeSection(next: HomeSection) {
    setSection(next);
    list.current?.scrollToOffset({ offset: 0, animated: false });
    Haptics.selectionAsync().catch(() => {});
  }

  async function uploadToDevice() {
    if (uploading) return;
    setUploading(true);
    try {
      if (!(await savedDisplay())) { setConnectOpen(true); return; }
      await uploadAnimation(current.image);
      setSelectedId(previewId);
      Haptics.notificationAsync(Haptics.NotificationFeedbackType.Success).catch(() => {});
      notify(current, 'Playing on display');
    } catch (error) {
      const message = error instanceof Error ? error.message : 'Upload failed';
      notify(current, message);
      if (/connect|Bluetooth|Peripheral|not found|offline/i.test(message)) setConnectOpen(true);
    } finally { setUploading(false); }
  }

  return (
    <>
    <FlatList
      ref={list}
      style={styles.root}
      data={visibleItems}
      keyExtractor={(item) => String(item.id)}
      numColumns={3}
      columnWrapperStyle={styles.row}
      initialNumToRender={12}
      maxToRenderPerBatch={9}
      windowSize={5}
      contentInsetAdjustmentBehavior="never"
      contentContainerStyle={{ paddingHorizontal: 24, paddingBottom: insets.bottom + 100 }}
      showsVerticalScrollIndicator={false}
      onLayout={() => SplashScreen.hideAsync()}
      ListHeaderComponent={
        <View style={{ paddingTop: insets.top + 10 }}>
          <StatusBar style="light" />
          <View style={[styles.hero, { width: heroWidth, height: heroHeight, marginHorizontal: -8 }]}>
            <BlurTargetView ref={blurTarget} style={StyleSheet.absoluteFill}>
              <GestureDetector gesture={swipeGesture}>
                <View
                  style={styles.heroTouch}
                  accessible
                  accessibilityRole="adjustable"
                  accessibilityLabel={`${current.name} animation preview`}
                  accessibilityActions={[{ name: 'increment', label: 'Next animation' }, { name: 'decrement', label: 'Previous animation' }]}
                  onAccessibilityAction={(event) => step(event.nativeEvent.actionName === 'increment' ? 1 : -1)}>
                  <Animated.View key={previewId} entering={FadeIn.duration(170)} exiting={FadeOut.duration(170)} style={[styles.heroImage, { top: 0 }]}>
                    <View style={{ width: heroSize, height: heroSize, borderRadius: heroSize / 2, overflow: 'hidden', backgroundColor: '#000' }}>
                      <Image
                        source={current.image}
                        contentFit="contain"
                        style={{ position: 'absolute', width: heroImageSize, height: heroImageSize, left: (heroSize - heroImageSize) / 2, top: (heroSize - heroImageSize) / 2 }}
                      />
                    </View>
                  </Animated.View>
                </View>
              </GestureDetector>
            </BlurTargetView>
            <BlurView blurTarget={blurTarget} blurMethod="dimezisBlurViewSdk31Plus" tint="dark" intensity={30} style={styles.heroFooter}>
              <View pointerEvents="none" style={styles.heroFooterFade} />
              <View style={styles.heroCopy}>
                <Text numberOfLines={1} style={styles.heroTitle}>{current.name}</Text>
                <Text numberOfLines={1} style={styles.heroSubtitle}>{current.category}</Text>
              </View>
              <Pressable accessibilityRole="button" accessibilityLabel={`Upload ${current.name} to display`} accessibilityHint="Long press to connect a different display" onPress={uploadToDevice} onLongPress={() => setConnectOpen(true)} disabled={uploading} style={({ pressed }) => [styles.uploadButton, pressed && styles.pressed]}>
                <Text style={styles.uploadText}>{uploading ? 'Uploading…' : 'Upload to Device'}</Text>
              </Pressable>
            </BlurView>
          </View>

          <View style={styles.segment}>
            {sections.map((option) => (
              <Pressable
                key={option}
                accessibilityRole="tab"
                accessibilityState={{ selected: section === option }}
                onPress={() => changeSection(option)}
                style={[styles.segmentButton, section === option && styles.segmentSelected]}>
                <Text style={[styles.segmentText, section === option && styles.segmentTextSelected]}>{option}</Text>
              </Pressable>
            ))}
          </View>

          {section === 'Packs' && (
            <View style={styles.features}>
              <ReferenceBanner title="Sharingan" width={width - 32} cropTop={1664} cropHeight={336} onPress={() => show(1, true)} />
              <ReferenceBanner title="Rorschach" width={width - 32} cropTop={2034} cropHeight={340} onPress={() => show(2, true)} />
            </View>
          )}
        </View>
      }
      ListEmptyComponent={section === 'My Library' ? (
        <View style={styles.empty}>
          <Text style={styles.emptyText}>No animation selected yet</Text>
          <Pressable accessibilityRole="button" onPress={() => changeSection('Individuals')} style={styles.emptyAction}>
            <Text style={styles.emptyActionText}>Browse animations</Text>
          </Pressable>
        </View>
      ) : null}
      renderItem={({ item }) => (
        <Pressable
          accessibilityRole="button"
          accessibilityLabel={`Show ${item.name}`}
          accessibilityState={{ selected: item.id === previewId }}
          onPress={() => show(item.id, true)}
          style={({ pressed }) => [styles.cell, { width: cellWidth, height: cellWidth }, item.id === previewId && styles.cellActive, pressed && styles.pressed]}>
          <DisplayBadge item={item} size={cellWidth * 0.78} />
          {item.id === selectedId && <View style={styles.selectedDot} />}
        </Pressable>
      )}
    />
    <ConnectDisplay visible={connectOpen} onClose={() => setConnectOpen(false)} onConnected={() => { setConnectOpen(false); uploadToDevice(); }} />
    </>
  );
}

function ReferenceBanner({ title, width, cropTop, cropHeight, onPress }: {
  title: string;
  width: number;
  cropTop: number;
  cropHeight: number;
  onPress: () => void;
}) {
  const scale = width / 1194;
  return (
    <Pressable accessibilityRole="button" accessibilityLabel={title} onPress={onPress} style={[styles.referenceBanner, { width, height: cropHeight * scale }]}>
      <Image
        source={require('../../assets/images/home-reference.png')}
        contentFit="fill"
        style={{ position: 'absolute', width: 1290 * scale, height: 2796 * scale, left: -48 * scale, top: -cropTop * scale }}
      />
    </Pressable>
  );
}

const styles = StyleSheet.create({
  root: { flex: 1, backgroundColor: INK },
  hero: { backgroundColor: '#000', overflow: 'hidden', borderRadius: 22, borderWidth: 1, borderColor: '#242426' },
  heroTouch: { flex: 1 },
  heroImage: { position: 'absolute', left: 0, right: 0, bottom: 0, alignItems: 'center', justifyContent: 'center' },
  heroFooter: { position: 'absolute', left: 0, right: 0, bottom: 0, height: 62, flexDirection: 'row', alignItems: 'flex-end', paddingHorizontal: 22, paddingBottom: 10, backgroundColor: 'rgba(0,0,0,0.24)', overflow: 'hidden' },
  heroFooterFade: { position: 'absolute', top: 0, right: 0, bottom: 0, left: 0, experimental_backgroundImage: 'linear-gradient(180deg, rgba(0,0,0,0) 0%, rgba(0,0,0,0.52) 52%, rgba(0,0,0,0.9) 100%)' },
  heroCopy: { flex: 1, paddingRight: 12 },
  heroTitle: { color: '#FFFFFF', fontSize: 14, fontWeight: '700', letterSpacing: -0.2 },
  heroSubtitle: { color: '#ACACB1', fontSize: 12, fontWeight: '600', marginTop: 3 },
  uploadButton: { minHeight: 30, borderRadius: 7, paddingHorizontal: 9, backgroundColor: '#333336', borderWidth: 1, borderColor: '#5B5B60', justifyContent: 'center' },
  uploadText: { color: '#EFEFF0', fontSize: 11, fontWeight: '700' },
  segment: { height: 32, borderRadius: 12, backgroundColor: '#19191B', flexDirection: 'row', padding: 2, marginHorizontal: -8, marginTop: 10, marginBottom: 12 },
  segmentButton: { flex: 1, borderRadius: 10, alignItems: 'center', justifyContent: 'center' },
  segmentSelected: { backgroundColor: '#39393C' },
  segmentText: { color: '#A9A9AE', fontSize: 13, fontWeight: '600' },
  segmentTextSelected: { color: '#F7F7F7' },
  features: { gap: 10, marginHorizontal: -8, marginBottom: 16 },
  referenceBanner: { borderRadius: 18, overflow: 'hidden', backgroundColor: '#151517' },
  row: { gap: 8, marginBottom: 8 },
  cell: { borderRadius: 20, backgroundColor: '#111113', borderWidth: 1, borderColor: '#252528', alignItems: 'center', justifyContent: 'center' },
  cellActive: { borderColor: '#F05850' },
  selectedDot: { position: 'absolute', top: 10, right: 10, width: 7, height: 7, borderRadius: 4, backgroundColor: CORAL },
  pressed: { opacity: 0.78 },
  empty: { alignItems: 'center', paddingVertical: 52, gap: 8 },
  emptyText: { color: '#AAA9AE', fontSize: 14 },
  emptyAction: { minHeight: 44, justifyContent: 'center', paddingHorizontal: 12 },
  emptyActionText: { color: CORAL, fontSize: 14, fontWeight: '700' },
  notificationBody: { flex: 1, flexDirection: 'row', alignItems: 'center', paddingLeft: 13, paddingRight: 20, gap: 12 },
  notificationCopy: { flex: 1 },
  notificationTitle: { color: CORAL, fontSize: 17, fontWeight: '700', letterSpacing: -0.35 },
  notificationMessage: { color: '#77787D', fontSize: 14, fontWeight: '500', letterSpacing: -0.2, marginTop: 1 },
});
