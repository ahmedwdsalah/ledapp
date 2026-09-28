import { DarkTheme, Stack, ThemeProvider } from 'expo-router';
import * as SplashScreen from 'expo-splash-screen';
import { GestureHandlerRootView } from 'react-native-gesture-handler';
import { SafeAreaProvider } from 'react-native-safe-area-context';

import { DynamicNotifications } from '@/components';
import { PreviewSelectionProvider } from '@/context/preview-selection';

SplashScreen.preventAutoHideAsync();

export default function RootLayout() {
  return (
    <GestureHandlerRootView style={{ flex: 1 }}>
      <SafeAreaProvider>
        <ThemeProvider value={DarkTheme}>
          <PreviewSelectionProvider>
            <DynamicNotifications accent="#F05850">
              <Stack screenOptions={{ headerShown: false, contentStyle: { backgroundColor: '#050507' } }} />
            </DynamicNotifications>
          </PreviewSelectionProvider>
        </ThemeProvider>
      </SafeAreaProvider>
    </GestureHandlerRootView>
  );
}
