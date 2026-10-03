# Spectrum workflow icons

- Source: [@adobe/spectrum-css-workflow-icons](https://www.npmjs.com/package/@adobe/spectrum-css-workflow-icons) 5.0.0 (`dist/assets/svg/S2_Icon_<Name>_20_N.svg`), from registry.npmjs.org.
- License: Apache License 2.0 (`LICENSE`, `COPYRIGHT`). © Adobe.
- Only the icons the app uses are kept, renamed to `<Name>.svg`. They color themselves with `var(--iconPrimary, …)`, which `IconProvider` replaces with the requested theme color when it loads them.

Icons that Spectrum lacks (the pen tools and a few others) are Leinwand's own, drawn on the same 20×20 grid, in `resources/icons/`.
