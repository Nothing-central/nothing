(function(profile) {
  'use strict';
  // profile injected as JSON via addScriptToInjectAtDocumentStart equivalent,
  // must run before any page script — earliest possible injection point.

  const defineGetter = (obj, prop, value) => {
    Object.defineProperty(obj, prop, {
      get: () => value,
      configurable: true,
      enumerable: true
    });
  };

  // ---- Navigator ----
  const nav = profile.navigator;
  defineGetter(Navigator.prototype, 'userAgent', nav.userAgent);
  defineGetter(Navigator.prototype, 'platform', nav.platform);
  defineGetter(Navigator.prototype, 'appVersion', nav.appVersion);
  defineGetter(Navigator.prototype, 'appName', nav.appName);
  defineGetter(Navigator.prototype, 'appCodeName', nav.appCodeName);
  defineGetter(Navigator.prototype, 'product', nav.product);
  defineGetter(Navigator.prototype, 'productSub', nav.productSub);
  defineGetter(Navigator.prototype, 'vendor', nav.vendor);
  defineGetter(Navigator.prototype, 'vendorSub', nav.vendorSub);
  defineGetter(Navigator.prototype, 'hardwareConcurrency', nav.hardwareConcurrency);
  defineGetter(Navigator.prototype, 'maxTouchPoints', nav.maxTouchPoints);
  defineGetter(Navigator.prototype, 'language', nav.languages[0]);
  defineGetter(Navigator.prototype, 'languages', Object.freeze([...nav.languages]));
  defineGetter(Navigator.prototype, 'webdriver', nav.webdriver);
  defineGetter(Navigator.prototype, 'doNotTrack', nav.doNotTrack);
  defineGetter(Navigator.prototype, 'onLine', nav.online);
  defineGetter(Navigator.prototype, 'cookieEnabled', nav.cookieEnabled);
  defineGetter(Navigator.prototype, 'pdfViewerEnabled', nav.pdfViewerEnabled);

  if (nav.deviceMemory !== undefined) {
    defineGetter(Navigator.prototype, 'deviceMemory', nav.deviceMemory);
  } else {
    delete Navigator.prototype.deviceMemory;
  }

  if (nav.userAgentData) {
    const uadata = {
      brands: nav.userAgentData.brands.map(b => ({ brand: b[0], version: b[1] })),
      mobile: nav.userAgentData.mobile,
      platform: nav.userAgentData.platform,
      getHighEntropyValues: async () => ({
        architecture: 'x86',
        bitness: '64',
        model: '',
        platformVersion: '10.0.0',
        uaFullVersion: nav.userAgentData.brands[0]?.[1] + '.0.0.0'
      }),
      toJSON: function() { return { brands: this.brands, mobile: this.mobile, platform: this.platform }; }
    };
    defineGetter(Navigator.prototype, 'userAgentData', uadata);
  } else {
    delete Navigator.prototype.userAgentData;
  }

  // ---- Screen ----
  const scr = profile.screen;
  defineGetter(Screen.prototype, 'width', scr.width);
  defineGetter(Screen.prototype, 'height', scr.height);
  defineGetter(Screen.prototype, 'availWidth', scr.width);
  defineGetter(Screen.prototype, 'availHeight', scr.height);
  defineGetter(Screen.prototype, 'availLeft', scr.avail_left);
  defineGetter(Screen.prototype, 'availTop', scr.avail_top);
  defineGetter(Screen.prototype, 'colorDepth', scr.color_depth);
  defineGetter(Screen.prototype, 'pixelDepth', scr.pixel_depth);

  defineGetter(window, 'devicePixelRatio', scr.device_pixel_ratio);
  defineGetter(window, 'outerWidth', scr.width);
  defineGetter(window, 'outerHeight', scr.height);
  defineGetter(window, 'screenX', scr.screen_x);
  defineGetter(window, 'screenY', scr.screen_y);
  defineGetter(window, 'screenLeft', scr.screen_x);
  defineGetter(window, 'screenTop', scr.screen_y);

  if (screen.orientation) {
    defineGetter(ScreenOrientation.prototype, 'type', scr.orientation_type);
    defineGetter(ScreenOrientation.prototype, 'angle', scr.orientation_angle);
  }

  // ---- matchMedia coherence with devicePixelRatio ----
  const realMatchMedia = window.matchMedia.bind(window);
  const dppx = scr.device_pixel_ratio;
  window.matchMedia = function(query) {
    const m = /min-resolution:\s*([\d.]+)dppx/.exec(query) ||
              /max-resolution:\s*([\d.]+)dppx/.exec(query);
    if (m) {
      const isMin = query.includes('min-resolution');
      const val = parseFloat(m[1]);
      const matches = isMin ? dppx >= val : dppx <= val;
      const mql = realMatchMedia(query);
      defineGetter(mql, 'matches', matches);
      return mql;
    }
    return realMatchMedia(query);
  };

  // ---- Timezone / Intl coherence ----
  const tzOffset = nav.timezone_offset_minutes;
  const RealDate = Date;
  Date.prototype.getTimezoneOffset = function() { return tzOffset; };

  const RealDTF = Intl.DateTimeFormat;
  const tz = nav.timezone;
  Intl.DateTimeFormat = new Proxy(RealDTF, {
    construct(target, args) {
      if (!args[1]) args[1] = {};
      if (!args[1].timeZone) args[1].timeZone = tz;
      return new target(...args);
    }
  });
  const realResolvedOptions = RealDTF.prototype.resolvedOptions;
  RealDTF.prototype.resolvedOptions = function() {
    const opts = realResolvedOptions.call(this);
    opts.timeZone = tz;
    opts.locale = nav.intl_locale;
    return opts;
  };

  // ---- webdriver hide (CDP-level flag still needs disabling separately) ----
  Object.defineProperty(navigator, 'webdriver', { get: () => nav.webdriver, configurable: true });

})(window.__NTH_FP_PROFILE__);