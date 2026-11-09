# esptool-js (vendored)

Espressif's [esptool-js](https://github.com/espressif/esptool-js), version
**0.7.0**, Apache License 2.0 (see `LICENSE`). The installer loads it from
here instead of a CDN, so the page runs only code from this repository.

- `bundle.js` is the unmodified `bundle.js` from the npm package
  `esptool-js@0.7.0`.
- npm tarball integrity:
  `sha512-9wHuW/s9t4Vs6HKcr3pra3gbRVWwaN0eabMWodWH4KEJHEYZxgmWwZ3s0F4xcWVCZOc/L98bGZ4uWoZvR8KBKA==`
- SHA-256 of `bundle.js`:
  `00ebe6fb0b202976d3e2a3c6b9a6bd438c26a81d2af6739c95ac276a64c1a84e`

To check it:

```sh
curl -sL -o esptool-js.tgz https://registry.npmjs.org/esptool-js/-/esptool-js-0.7.0.tgz
tar xzf esptool-js.tgz package/bundle.js
shasum -a 256 package/bundle.js installer/vendor/esptool-js/bundle.js
```

The installer works around two problems in this version rather than patching
it: the hard reset never raises RTS, and `readFlash` neither limits how much
data is in flight nor checks the MD5 the flasher sends after the data. See
`installer/js/device.js` and `installer/js/flashio.js`.
