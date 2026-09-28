const { withAndroidManifest } = require('expo/config-plugins');

module.exports = function withMotifNetwork(config) {
  return withAndroidManifest(config, (next) => {
    const application = next.modResults.manifest.application?.[0];
    if (application) application.$['android:usesCleartextTraffic'] = 'true';
    return next;
  });
};
