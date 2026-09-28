import { useIsFocused } from 'expo-router';
import { View } from 'react-native';

import AnimationsScreen from '@/components/animations-screen';

export default function AnimationsTab() {
  const focused = useIsFocused();
  return focused ? <AnimationsScreen /> : <View style={{ flex: 1, backgroundColor: '#08090B' }} />;
}
