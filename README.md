# SIGNAL RACK

Browser-based audio tools built with the Web Audio API. No install, no server, no upload — everything runs locally in the browser tab.

## Tools

| Unit | File | What it does |
|---|---|---|
| 01 | `wavebender.html` | Phaser, flanger, and live pitch-bend on a single track |
| 02 | `trackbender.html` | Synced multitrack (up to 12) player with per-track pitch shift, BPM-synced delay, and EQ |
| 03 | `final-stage.html` | Mastering chain — limiter, exciter, stereo imager, reverb (reorderable) — with a loudness meter and WAV export |

`index.html` is the landing page linking to all three.

## Hosting on GitHub Pages

1. Create a new GitHub repository (public).
2. Upload all files in this folder (`index.html`, `wavebender.html`, `trackbender.html`, `final-stage.html`) to the repository root.
3. In the repo, go to **Settings → Pages**.
4. Under **Build and deployment → Source**, choose **Deploy from a branch**.
5. Set **Branch** to `main` (or your default branch) and folder to `/ (root)`, then **Save**.
6. GitHub will publish the site at `https://<your-username>.github.io/<repo-name>/` within a minute or two.

## Notes

- All audio processing happens client-side; nothing the user loads is ever uploaded anywhere.
- Recommended browsers: latest Chrome, Edge, or Safari.
