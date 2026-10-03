# Leinwand — notices

Leinwand is free software under the GNU General Public License, version 3 or
later (`LICENSE`). Builds that include KDDockWidgets can be distributed under
GPL version 3 only.

Leinwand is not affiliated with, endorsed by or sponsored by Adobe Inc.
Adobe, Illustrator and Spectrum are trademarks or registered trademarks of
Adobe Inc. in the United States and/or other countries. "Source" is a
trademark of Adobe Inc. (font family names below).

## Third-party software and assets

| Component | Use | License |
| --- | --- | --- |
| [Qt](https://www.qt.io/) 6.8 | User interface. Linked dynamically; the libraries can be replaced with a compatible build. Source: https://download.qt.io/ | GNU LGPL v3 |
| [Skia](https://skia.org/) | Drawing | BSD 3-Clause, © Google LLC |
| [KDDockWidgets](https://github.com/KDAB/KDDockWidgets) 2.4.1 (patched, see `cmake/patches`) | Panel docking | GPL v2 or GPL v3, © Klarälvdalens Datakonsult AB |
| [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | GPU memory | MIT, © Advanced Micro Devices, Inc. |
| [nlohmann/json](https://github.com/nlohmann/json) | Theme conversion, file format | MIT, © Niels Lohmann |
| [miniz](https://github.com/richgel999/miniz) | ZIP container of .lwd files | MIT, © Rich Geldreich and contributors |
| [pugixml](https://pugixml.org/) | SVG import | MIT, © Arseny Kapoulkine |
| [libpng](http://www.libpng.org/), [libjpeg-turbo](https://libjpeg-turbo.org/), [zlib](https://zlib.net/), [Expat](https://libexpat.github.io/) | Image and PDF support in Skia | libpng license; IJG and BSD-3-Clause; zlib license; MIT |
| [Spectrum design tokens](https://github.com/adobe/spectrum-tokens) 15.5.0 | Colors and sizes of the user interface | Apache License 2.0, © Adobe (`licenses/LICENSE`) |
| [Spectrum CSS workflow icons](https://github.com/adobe/spectrum-css-workflow-icons) 5.0.0 | Icons | Apache License 2.0, © Adobe |
| [Source Sans 3](https://github.com/adobe-fonts/source-sans) 3.052 | User interface font | SIL Open Font License 1.1 (`licenses/LICENSE-SourceSans3.md`) |
| [Source Han Sans](https://github.com/adobe-fonts/source-han-sans) 2.005 (JP subset) | Japanese user interface font | SIL Open Font License 1.1 (`licenses/LICENSE-SourceHanSans.txt`) |

The Apache-licensed Spectrum assets are used unmodified except that the icons
are recolored at run time. The workflow icons' copyright notice: "© Copyright
2015-2024 Adobe. All rights reserved."
