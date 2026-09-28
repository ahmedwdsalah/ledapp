import { useIsFocused } from 'expo-router';
import { View } from 'react-native';

import DisplayScreen from '@/components/display-screen';

export default function DisplayTab() {
  const focused = useIsFocused();
  return focused ? <DisplayScreen /> : <View style={{ flex: 1, backgroundColor: '#08090B' }} />;
}
