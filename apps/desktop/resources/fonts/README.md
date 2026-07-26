# Fonts

Nightlock locks its UI font to San Francisco on every platform (see
`src/fonts.cpp`); only the tree, titles and entry names use Georgia.

macOS ships San Francisco built in, so this directory can stay empty
there. On Windows and Linux, download the SF Pro package from
<https://developer.apple.com/fonts/> and drop the files here (e.g.
`SF-Pro-Text-Regular.otf`, `SF-Pro-Text-Semibold.otf`,
`SF-Pro-Display-Bold.otf`) — the app loads every `*.otf` / `*.ttf`
from this directory at startup.

Apple's license does not permit redistributing the fonts, which is why
they are not committed to the repository (`.gitignore` next to this
file keeps them out).
