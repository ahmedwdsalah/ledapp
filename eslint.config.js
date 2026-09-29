// https://docs.expo.dev/guides/using-eslint/
const { defineConfig } = require('eslint/config');
const expoConfig = require("eslint-config-expo/flat");

module.exports = defineConfig([
  expoConfig,
  {
    ignores: ["dist/*"],
  },
  {
    files: [
      "src/hooks/use-notification-timeline.ts",
      "src/hooks/use-dismiss-gesture.ts",
    ],
    rules: {
      "react-hooks/immutability": "off",
    },
  },
  {
    files: ["src/components/display-screen.tsx"],
    rules: {
      "react-hooks/refs": "off",
    },
  },
]);
