# Crank'n Sail: marketing plan

Goal: sell a few hundred $3 copies to Playdate owners without spending money. Playdate's audience is small and tight-knit, so the plan is to be genuinely useful and visible in the places they already read, and make "try the demo" the lowest-friction step.

## 1. What we have to work with

| Asset | State |
| --- | --- |
| Game | Playdate build, 4 boats (dinghy, Sunfish, J/24, Reynolds 33 cat), Free Range / Journey / Racing, 7 weathers |
| Demo | `make DEMO=1`: Free Range only, 5 minutes a run |
| Web version | `web/` (WASM + Three.js), live on GitHub Pages. A `make -C web DEMO=1` build exists too |
| Releases | `tools/release.sh vX.Y.Z` builds full + demo `.pdx.zip` files on GitHub |
| Launcher art | Card + launch animation in `Source/launcher/` |
| Footage | `dinghy_weather_orbit` (7 clips: mist, sunny, cloudy, choppy, rollers, storm, hurricane; about 8 s each), `dinghy_buoy_pass_rollers.mp4` (about 9 s, "Are you ready to sail?" caption), an 800 px GIF of the buoy pass. All 1600x960, 30 fps |

**The hook** (one line to reuse everywhere): *"A sailing sim you steer with the Playdate crank. Real boat physics, five boats' worth of heel, capsize and weather, in 1-bit."* Lead with the crank-as-helm/trim and the weather range; those are visual and unique.

**Open decision: the AGPL licence.** The source is public under AGPLv3, so anyone can build the game free. That is fine and arguably good for goodwill, but the $3 is then "buy it prebuilt, support the dev", not DRM-protected. Say so openly on the page ("source on GitHub; $3 buys the ready-to-sideload build and funds updates"). Players respond well to that, and it pre-empts the "why pay?" comment.

## 2. itch.io release

**Page setup**
- Project kind: *Downloadable*. Tags: `playdate`, `sailing`, `simulation`, `1-bit`, `crank`, `physics`. Genre: Simulation. Add to the Playdate collection/jam listings if you can.
- Pricing: **$3 minimum, paid**, with the demo as a free file on the *same page* (itch shows demos in a section under the buy box once a minimum price is set). Fallback if the demo section is fiddly: a separate "Crank'n Sail Demo" page, free, linking to the paid one. Check the dashboard before launch day; this behaviour is reported by users, not guaranteed in the docs. Note that on a same-page setup, free claims (e.g. giveaway keys) may only retain the demo files afterwards, so don't run free sales there.
- Files: `CranknSail-vX.pdx.zip` (paid) and `CranknSail-Demo-vX.pdx.zip` (demo), both from the GitHub Release workflow. Tick the Playdate platform if offered, otherwise "Other".
- Embed the **web demo** (HTML5, `make -C web DEMO=1`) as a "Play in browser" option. Many people will try it without owning a Playdate; some of those convert later. Keep it the demo build only.
- Cover image 630x500 (itch spec): a still of the boat mid-heel from the buoy pass. Screenshots: one per weather (storm and hurricane are the draw), one HUD shot, one menu shot. Use the 4x nearest-neighbour framing so pixels stay crisp.
- Trailer: the stitched video (section 4), uploaded to YouTube and linked in the itch video field.
- Description structure: hook line, GIF, 5 bullets (boats, weather, modes, crank controls, free demo), "How to sideload" (3 steps: download, Playdate web sideload page or USB, open), controls table (copy from README), credits and source link.
- Revenue share: itch's default is 10% and it is adjustable; leave it default or lower it. Payment fees come on top.
- Devlog post on launch day, then one a week for the first month (patch notes, a boat-physics note, a new course).

**Also do (all free)**
- Submit to the Playdate Catalog only if you want curated store placement; it has its own process and timeline, so treat it as a later phase and check [play.date](https://play.date) for current rules.
- Post in the Playdate Dev Forum showcase thread (devforum.play.date).
- Tag the GitHub Release `v1.0.0` so sideloaders have a second route.

## 3. Reddit (free, organic)

I could not verify each subreddit's current rules, so **read each sidebar and pinned post the day before posting**. Common patterns: a free way to try the game at post time (we have the demo and the web build), a cap on self-promo as a share of your activity, minimum account age or karma, and flair requirements. Message the mods if unsure; they usually say yes to a transparent dev post.

**Before launch (2 weeks out):** spend a week genuinely participating (comment on others' Playdate posts, answer questions). Reddit's 10% self-promo rule of thumb is real, and an account that only posts links gets flagged.

| Subreddit | Angle | When |
| --- | --- | --- |
| r/PlaydateConsole (check whether r/playdate is also active) | "Made a sailing sim for Playdate; free demo, $3 full" with the 9 s buoy-pass clip | Launch day (Tue to Thu, morning US Eastern) |
| r/WebGames | "Sail in your browser, crank = your mouse" with the web demo link | Day 2 |
| r/indiegaming, r/playmygame | Demo-first post; playmygame wants a playable link | Day 3 to 5 |
| r/sailing, r/Sailboats, r/dinghysailing | Nerd angle: the boat model (heel, capsize at 50 deg on cats, sail trim). Only if rules allow; lead with the physics, not the sale | Week 2 |
| r/gamedev, r/IndieDev | Dev post: "how I modelled a sailboat in C on a 1-bit handheld", with the GitHub link. Use the Saturday/feedback threads where required | Week 2 to 3 |
| r/opensource, r/PlaydateConsole | Source release angle (AGPL) | Week 3 |

**Post recipe**
1. Title: plain and specific, e.g. *"I made a sailing sim for the Playdate where the crank is your helm (free demo)"*. No hype, no emoji.
2. Native video or GIF in the post (Reddit favours its own hosting), not just a link. Use the cut-downs from section 4.
3. First comment from you within a minute: what it is, how the controls work, link to the itch page, demo vs full ($3), source link, and one question to invite replies ("what boat should I add?").
4. Reply to every comment for the first 3 hours. Take bug reports and feature asks seriously and say what shipped.
5. One post per subreddit, never crossposted blindly. Vary the title and video per audience.
6. Follow-ups only with news: a new boat, a patch, a milestone ("v1.1: new course, ghost sharing").

## 4. Stitching the footage into a trailer

### Deliverables

| Cut | Length | Where it goes |
| --- | --- | --- |
| **Trailer** 16:9 | 45 to 60 s | itch.io page, YouTube, X/Bluesky, the Playdate forum |
| **Hook clip** | 9 to 12 s, loops | Reddit posts, the top of the itch description |
| **Vertical 9:16** (optional) | 15 to 20 s | Shorts/Reels/TikTok |
| **GIF** | 800 px, under 3 MB | itch description, forum posts (the buoy-pass GIF already exists) |

### Trailer storyboard (about 55 s)

| t | Shot | Source | On-screen text |
| --- | --- | --- | --- |
| 0:00 to 0:09 | Buoy pass: the boat sails behind the buoy | `dinghy_buoy_pass_rollers.mp4` | "Are you ready to sail?" (already burned in) |
| 0:09 to 0:14 | Sunny orbit, first half | `dinghy_sunny.mp4` | "A sailing simulator for Playdate" |
| 0:14 to 0:19 | Mist | `dinghy_mist.mp4` | "Steer, trim and hoist with the crank" |
| 0:19 to 0:24 | Cloudy / choppy, quick cuts | `dinghy_cloudy.mp4`, `dinghy_choppy.mp4` | "Real boat physics" |
| 0:24 to 0:32 | Rollers, storm | `dinghy_rollers.mp4`, `dinghy_storm.mp4` | "Seven kinds of weather" |
| 0:32 to 0:40 | Hurricane (the payoff) | `dinghy_hurricane.mp4` | "Reef. Or capsize." |
| 0:40 to 0:48 | **New footage needed**: a J/24 and the cat, a race with a ghost, a HUD shot | `promo_video.py series weather-orbit --boat j24 --hud` | "Four boats. Free Range, Journey, Racing" |
| 0:48 to 0:55 | End card | title art from `Source/launcher/` | "Crank'n Sail. Free demo on itch.io. $3." + URL |

Pacing rule: 3 to 5 s per shot, cut on a wave or a heel peak, hardest weather last. Every shot is only the dinghy at the moment, so the **boat variety and a HUD/gameplay shot are the main gaps**: record them before editing (command above, plus `--boat sunfish`, `--boat reynolds33`; check `boats/` for exact names). A shot of the real device in hand with the crank turning is the single most persuasive asset for a Playdate audience; film one 10 s clip on a phone, steady, daylight.

### Tooling and steps

`ffmpeg` is not installed on this machine (`sudo apt install ffmpeg`). Everything below is scriptable, so the whole edit is reproducible and re-runnable when footage changes.

1. **Collect**: unzip `~/Downloads/dinghy_weather_orbit.zip` and copy `dinghy_buoy_pass_rollers.mp4` into `build/promo/` (gitignored). Keep the 1600x960 masters.
2. **Normalise** every clip to one frame: 1920x1080, 30 fps, no scaling of the game pixels. 1600x960 is a 4x integer scale, so centre it on a 1920x1080 canvas filled with the game's background grey instead of resizing (resizing blurs the 1-bit pixels):
   ```bash
   ffmpeg -i in.mp4 -vf "pad=1920:1080:(ow-iw)/2:(oh-ih)/2:color=0xB1AFA8,fps=30" -c:v libx264 -crf 14 -pix_fmt yuv420p norm.mp4
   ```
   Alternatively record again with `--scale 4.5` equivalents; integer scales keep crisp pixels, so padding is preferred.
3. **Trim** each shot to its storyboard length (`-ss`/`-t`, or the `trim` filter).
4. **Join with short cross-fades** (0.25 s `xfade`) or hard cuts; hard cuts suit this style. Use the concat demuxer for hard cuts, `xfade` for fades.
5. **Captions**: `drawtext` with the Silkscreen or Jersey 10 font that the game already uses (`Source/fonts/` or `assets/fonts/`), so the text matches the game. White on dark grey, bottom third, 2 to 3 s each.
6. **Music**: royalty-free, calm and rhythmic (CC0 sources such as Pixabay or Free Music Archive; record the licence in this folder). Fade in and out; duck nothing, as there is no voice-over. Add one small wave/wind loop if the game has audio capture (`src/pd/` has audio code).
7. **Cut-downs**: hook clip = the buoy pass, looped once, no music needed. GIF: `ffmpeg ... -vf "fps=15,scale=800:-1:flags=neighbor,palettegen"` then `paletteuse`. Vertical 9:16: crop the centre 540x960 of the 1600x960 frame, or place the full frame on a 1080x1920 canvas with the title above and the caption below.
8. **QA**: watch muted (most Reddit views are muted, so captions must carry it), then on a phone. Check the first frame is a strong still, since it becomes the thumbnail.
9. **Export**: H.264 MP4, 1920x1080, about 8 to 12 Mbit/s, AAC 192k; keep under Reddit's size limit by using the shorter cut-downs there.

`tools/stitch_trailer.sh` does steps 2 to 6 and 9 from a storyboard file (`tools/trailer.txt`, lines of `clip start duration caption`):

```bash
tools/stitch_trailer.sh tools/trailer.txt build/promo/trailer.mp4 --music track.mp3 --vertical
```

## 5. Timeline

| When | What |
| --- | --- |
| Week -2 | Install ffmpeg, record missing footage (boats, HUD, device-in-hand), start joining Reddit conversations, read subreddit rules |
| Week -1 | Edit trailer and cut-downs, build itch page as a **draft** (private), test the paid file and the demo download from a second account, test the sideload flow on a real Playdate |
| Launch day (Tue to Thu) | Publish itch page, YouTube trailer, r/PlaydateConsole post, Playdate forum post, devlog #1 |
| Day 2 to 5 | r/WebGames, r/indiegaming, r/playmygame; answer everything |
| Week 2 to 3 | r/sailing-type physics post, r/gamedev dev post; devlog #2 with fixes from feedback |
| Month 2 | v1.1 update with a user-requested feature; repost to r/PlaydateConsole as an update; consider a launch-discount weekend ($2) if sales are flat |

## 6. Measure and adjust

- itch.io analytics: views, demo downloads, browser plays, purchases; compute demo to paid conversion. Reference points to watch: views per Reddit post, demo downloads per 100 views, paid per 100 demos.
- Use itch's referrer data to learn which subreddit and which video worked; double down on those.
- Triage feedback: crashes first, then the top-requested feature. Ship fixes within days; fast replies are free marketing in a community this small.
- If demo downloads are high but sales are low, tighten the demo's ending ("Thanks for playing" screen) with the price and where to buy.

## Sources

- itch.io demo behaviour: [itch.io forum, "how do i add a demo to my game"](https://itch.io/t/52539/how-do-i-add-a-demo-to-my-game), [game demo and pricing](https://itch.io/t/338657/game-demo-and-pricing), [Playdate collection](https://itch.io/c/3049142/playdate)
- Playdate promotion advice: [Playdate Dev Forum, "Advice for promoting your game?"](https://devforum.play.date/t/advice-for-promoting-your-game/6213)
- Reddit self-promo patterns: [How to promote your indie game on Reddit without getting banned](https://gtstu.com/promote-indie-game-reddit-without-ban/) (general guidance, not subreddit-specific)
