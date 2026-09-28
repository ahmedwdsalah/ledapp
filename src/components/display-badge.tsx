import { Image } from 'expo-image';
import { StyleSheet, View, type StyleProp, type ViewStyle } from 'react-native';

import type { GalleryItem } from '@/constants/gallery-art';

export function DisplayBadge({ item, size, style }: { item: GalleryItem; size: number; style?: StyleProp<ViewStyle> }) {
  return (
    <View accessible={false} style={[{ width: size, height: size, borderRadius: size / 2, overflow: 'hidden', backgroundColor: '#000000' }, style]}>
      {item.framed ? (
        <Image source={item.image} contentFit="cover" style={StyleSheet.absoluteFill} />
      ) : (
        <View style={StyleSheet.absoluteFill}>
          <Image source={item.image} contentFit="contain" style={{ position: 'absolute', left: size * 0.12, right: size * 0.12, top: size * 0.12, bottom: size * 0.12 }} />
          <Image source={require('../../assets/images/emblem-bezel.png')} contentFit="cover" style={StyleSheet.absoluteFill} />
        </View>
      )}
    </View>
  );
}
