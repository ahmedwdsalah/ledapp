import { router } from 'expo-router';
import { Image } from 'expo-image';
import * as Haptics from 'expo-haptics';
import * as SplashScreen from 'expo-splash-screen';
import { StatusBar } from 'expo-status-bar';
import { useMemo, useRef, useState } from 'react';
import { Image as NativeImage, Platform, Pressable, StyleSheet, Text, View } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import Animated, { FadeIn, FadeInUp, useReducedMotion } from 'react-native-reanimated';

import { galleryItems } from '@/constants/gallery-art';
import { usePreviewSelection } from '@/context/preview-selection';
import InfiniteMenu from '@/shared/components/organisms/infinite-menu';

export default function WelcomeScreen() {
  const insets = useSafeAreaInsets();
  const reducedMotion = useReducedMotion();
  const { setPreviewId } = usePreviewSelection();
  const didHideNativeSplash = useRef(false);
  const [interacted, setInteracted] = useState(false);
  const menuItems = useMemo(() => galleryItems.slice(0, 20).map((item) => ({
    image: NativeImage.resolveAssetSource(item.image).uri,
    title: item.name,
  })), []);

  function hideNativeSplash() {
    if (didHideNativeSplash.current) return;
    didHideNativeSplash.current = true;
    requestAnimationFrame(() => SplashScreen.hideAsync());
  }

  return (
    <View style={styles.root} onLayout={hideNativeSplash}>
      <StatusBar style="light" />

      <InfiniteMenu
        items={menuItems}
        backgroundColor="#050507"
        style={styles.menu}
        autoRotateSpeed={interacted || reducedMotion ? 0 : 0.1}
        onInteractionStart={() => setInteracted(true)}
        onItemPress={(_item, index) => {
          setPreviewId(index);
          Haptics.selectionAsync().catch(() => {});
        }}
      />

      <Animated.View pointerEvents="none" entering={FadeIn.duration(550)} style={[styles.brand, { top: insets.top + 30 }]}>
        <Image source={require('../../assets/images/motif-mark.png')} style={styles.mark} contentFit="contain" />
        <Text style={styles.wordmark}>MOTIF</Text>
      </Animated.View>

      <View pointerEvents="none" style={[styles.footerFade, { height: insets.bottom + 300 }]} />
      <Animated.View pointerEvents="box-none" entering={FadeInUp.delay(reducedMotion ? 0 : 850).duration(reducedMotion ? 150 : 500)} style={[styles.footer, { paddingBottom: insets.bottom + 26 }]}>
        {(Platform.OS === 'android' ? ['google', 'apple'] : ['apple', 'google']).map((provider, index) => (
          <SignInButton key={provider} provider={provider as 'apple' | 'google'} primary={index === 0} onPress={() => router.replace('/studio')} />
        ))}
      </Animated.View>
    </View>
  );
}

function SignInButton({ provider, primary, onPress }: { provider: 'apple' | 'google'; primary: boolean; onPress: () => void }) {
  return (
    <Pressable accessibilityRole="button" onPress={onPress} style={({ pressed }) => [styles.signInButton, primary ? styles.primaryButton : styles.secondaryButton, pressed && styles.pressedButton]}>
      <Image
        source={provider === 'apple' ? require('../../assets/images/apple-logo.png') : require('../../assets/images/google-g-logo.png')}
        style={styles.providerIcon}
        contentFit="contain"
        tintColor={provider === 'apple' ? (primary ? '#111111' : '#FFFFFF') : undefined}
      />
      <Text style={[styles.providerLabel, !primary && styles.secondaryLabel]}>Continue with {provider === 'apple' ? 'Apple' : 'Google'}</Text>
    </Pressable>
  );
}

const styles = StyleSheet.create({
  root: { flex: 1, backgroundColor: '#050507', overflow: 'hidden' },
  menu: { flex: 1 },
  brand: { position: 'absolute', left: 27, right: 27, flexDirection: 'row', alignItems: 'center', gap: 11 },
  mark: { width: 29, height: 29 },
  wordmark: { color: '#FFFFFF', fontSize: 17, fontWeight: '800', letterSpacing: 4 },
  footerFade: {
    position: 'absolute', left: 0, right: 0, bottom: 0,
    experimental_backgroundImage: 'linear-gradient(180deg, rgba(5,5,7,0) 0%, rgba(5,5,7,0.16) 24%, rgba(5,5,7,0.55) 57%, rgba(5,5,7,0.82) 100%)',
  },
  footer: { position: 'absolute', left: 0, right: 0, bottom: 0, paddingHorizontal: 25, paddingTop: 25, gap: 10 },
  signInButton: { width: '100%', height: 54, borderRadius: 13, flexDirection: 'row', alignItems: 'center', justifyContent: 'center', gap: 11 },
  pressedButton: { transform: [{ scale: 0.98 }] },
  primaryButton: { backgroundColor: '#FFFFFF' },
  secondaryButton: { borderWidth: 1, borderColor: 'rgba(255,255,255,0.42)', backgroundColor: 'rgba(17,17,19,0.42)' },
  providerIcon: { width: 20, height: 20 },
  providerLabel: { color: '#101010', fontSize: 16, fontWeight: '600' },
  secondaryLabel: { color: '#F7F7F7' },
});
