import { NativeTabs } from 'expo-router/unstable-native-tabs';

export default function AppTabs() {
  return (
    <NativeTabs
      backgroundColor="#08090B"
      indicatorColor="#252428"
      tintColor="#F05850"
      labelStyle={{ selected: { color: '#FFFFFF' } }}>
      <NativeTabs.Trigger name="studio" contentStyle={{ backgroundColor: '#08090B' }}>
        <NativeTabs.Trigger.Label>Display</NativeTabs.Trigger.Label>
        <NativeTabs.Trigger.Icon
          sf="circle"
          src={require('@/assets/images/tabIcons/home.png')}
          renderingMode="template"
        />
      </NativeTabs.Trigger>

      <NativeTabs.Trigger name="explore" contentStyle={{ backgroundColor: '#08090B' }}>
        <NativeTabs.Trigger.Label>Animations</NativeTabs.Trigger.Label>
        <NativeTabs.Trigger.Icon
          sf="square.grid.2x2"
          src={require('@/assets/images/tabIcons/explore.png')}
          renderingMode="template"
        />
      </NativeTabs.Trigger>
    </NativeTabs>
  );
}
