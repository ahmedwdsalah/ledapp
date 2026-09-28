pnpm expo start --dev-client -c
eas build --platform ios --profile development
eas build --platform ios --profile production

eas.json  >>> "distribution": "internal", "distribution": "store",

## Dependency Parity Note
- If `package.json` Expo / React Native versions are updated for parity, run `pnpm install` in `/Users/ahmed/Makan` before the next `pnpm expo start --dev-client -c` or EAS build.

## Refactor Validation Runbook
- `pnpm exec eslint app/_layout.tsx app/settings/index.tsx app/settings/change-password.tsx`
- `pnpm exec tsc --noEmit`
- `rg -n "supabase\\.auth\\.signOut|delete-account|updateUser\\({ password|onAuthStateChange|getSession\\(" app hooks`
- `rg -n "customer-avatars|\\bcustomer\\b|consumer_profile|consumer-avatars" app hooks lib scripts`
