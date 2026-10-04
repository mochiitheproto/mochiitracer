/************************************************************
 * MochiiTracer · Protogen OS - web firmware installer
 *
 * Adapted from coelacant1's ProtoTracer firmware uploader
 * (.docs/prototracer-firmware-uploader.js in https://github.com/coelacant1/ProtoTracer).
 * Licensed under the GNU AGPL-3.0, like ProtoTracer.
 *
 * The flashing logic is the original one:
 *  - Intel HEX parsing -> 1 KB blocks (addresses relative to 0x60000000)
 *  - WebHID upload to the Teensy 4 bootloader (HalfKay): 1088-byte reports
 *    [3 address bytes + 61 zero + 1024 data], blank blocks skipped except the
 *    first, 1.5 s after the first block (erase), 5 ms after the rest,
 *    final 0xFF 0xFF 0xFF report to reboot.
 *
 * Changes (details in README.md):
 *  - Firmware comes from the mochiitheproto/mochiitracer releases, one .hex per
 *    species (protogen / primagen) and language. GitHub release downloads send
 *    no CORS headers, so the page first looks for a same-origin copy in
 *    firmware/<tag>/ and otherwise links the file for a manual download. When
 *    the release lists a SHA-256, the download is checked against it.
 *  - Fix: a refused report is retried for up to 20 s instead of 5 times in
 *    0.5 s. HalfKay refuses reports while the erase from the first block runs,
 *    and over a large old firmware that took longer than the 5 tries.
 *  - Fix: gaps in the HEX keep their address. The original compacted the block
 *    list with filter(), which moves every block after a gap to a wrong address.
 *  - The image is checked before anything is erased: Teensy 4 FlexSPI tag,
 *    board from the flash size it was built for, and that it fits.
 *    The file name no longer has to contain "teensy40".
 *  - Restart into the bootloader over WebSerial: opening the Teensy's serial
 *    port at 134 baud makes Teensyduino's USB stack jump to the bootloader.
 *  - Serial console that can send commands, and closes cleanly (waits for the
 *    read pipe before close(), which the original skipped).
 ************************************************************/
(function (global) {
    "use strict";

    // ===== Configuration =====
    const REPO = "mochiitheproto/mochiitracer";
    const ASSET_PREFIX = "mochiitracer-teensy40-";   // + <species>-<lang>.hex
    const SPECIES = ["protogen", "primagen"];        // -D ESPECIE_PRIMAGEN or not
    const FW_LANGS = ["es", "en", "zh"];             // -D IDIOMA_DEFECTO=0|1|2, and l0|l1|l2 (l255 = the build's)
    const STORE = "mochiitracer-";                   // localStorage key prefix
    // l<N> switches the head's language (firmware v1 and newer) and the head keeps it in EEPROM,
    // even across installs; l255 forgets it, so the head goes back to the language its .hex was
    // built with. Keep in sync with the firmware.
    const HEAD_LANG_CMD = { es: "l0", en: "l1", zh: "l2", build: "l255" };
    // Same-origin copy of the release assets: firmware/<tag>/<asset>, plus
    // firmware/manifest.json with the releases and each file's SHA-256. Both come
    // straight from the release build (see README.md).
    const MIRROR_DIR = "firmware/";
    const RELEASES_API = `https://api.github.com/repos/${REPO}/releases?per_page=15`;
    const REPO_URL = `https://github.com/${REPO}`;

    const TEENSY_VID = 0x16C0;
    const HALFKAY_FILTERS = [
        { vendorId: 0x16C0, productId: 0x0478 }, // HalfKay bootloader
        { vendorId: 0x16C0, productId: 0x0479 }, // listed by the original uploader as Teensy 4.1
    ];
    const REBOOT_BAUD = 134;      // cores/teensy4/usb.c: SET_LINE_CODING at 134 baud -> bootloader
    const CONSOLE_BAUD = 115200;  // Serial.begin(115200) in src/main.cpp (USB serial ignores it anyway)

    const FLASH_BASE = 0x60000000;
    const BLOCK_SIZE = 1024;
    const REPORT_SIZE = BLOCK_SIZE + 64; // 1088
    const FCFB_TAG = 0x42464346;         // "FCFB", FlexSPI config block at the start of flash
    const RETRY_BUDGET_MS = 20000;       // per report; a 2 MB erase must fit (see sendReportWithRetries)
    const RETRY_GAP_MS = 20;
    // Flash size at offset 0x50 of the FlexSPI config (cores/teensy4/bootdata.c) and the
    // usable code size teensy_loader_cli uses for each board.
    const BOARDS = {
        0x00200000: { name: "Teensy 4.0", code: 2031616 },
        0x00800000: { name: "Teensy 4.1", code: 8126464 },
        0x01000000: { name: "Teensy MicroMod", code: 16515072 },
    };
    const TEENSY40 = BOARDS[0x00200000];

    // ===== Utility Functions =====
    function sleep(ms) {
        return new Promise(resolve => setTimeout(resolve, ms));
    }

    function assetName(species, lang) {
        return `${ASSET_PREFIX}${species}-${lang}.hex`;
    }

    // "sha256:<hex>" (GitHub API digest) or a bare hex string (manifest.json)
    function sha256Of(a) {
        const v = String(a.sha256 || a.digest || "").toLowerCase().replace(/^sha256:/, "");
        return /^[0-9a-f]{64}$/.test(v) ? v : null;
    }

    async function sha256Hex(bytes) {
        const d = await crypto.subtle.digest("SHA-256", bytes);
        return Array.from(new Uint8Array(d), b => b.toString(16).padStart(2, "0")).join("");
    }

    function langFromNavigator(languages) {
        for (const raw of languages || []) {
            const l = String(raw).toLowerCase();
            if (l.startsWith("zh")) return "zh";
            if (l.startsWith("es")) return "es";
            if (l.startsWith("en")) return "en";
        }
        return "en";
    }

    function looksLikeHex(bytes) {
        for (let i = 0; i < Math.min(bytes.length, 16); i++) {
            const c = bytes[i];
            if (c === 0x3A) return true;                         // ':'
            if (c !== 0xEF && c !== 0xBB && c !== 0xBF && c !== 0x20 && c !== 0x0D && c !== 0x0A && c !== 0x09) return false;
        }
        return false;
    }

    // ===== Intel HEX Parsing Logic =====
    // Parse a `.hex` into 1KB blocks, skipping addresses below the offset.
    // Returns a sparse array indexed by block number: blocks[i] covers
    // offset + i*blockSize. Blocks with no data stay undefined.
    async function parseHexToBlocks(hexData, blockSize, offset) {
        const text = new TextDecoder().decode(hexData);

        const lines = text.split(/\r?\n/);
        const blocks = [];
        let baseAddress = 0;

        function ensureBlock(idx) {
            if (!blocks[idx]) {
                const block = new Uint8Array(blockSize);
                block.fill(0xff);
                blocks[idx] = block;
            }
        }

        for (let lineNum = 0; lineNum < lines.length; lineNum++) {
            let line = lines[lineNum].trim();
            if (!line) continue;
            if (!line.startsWith(':')) {
                throw new Error(`Invalid HEX: missing ':' on line ${lineNum+1}`);
            }

            line = line.slice(1);
            // parseInt() turns bad digits into NaN, and NaN slips through the checksum test
            if (!/^[0-9A-Fa-f]+$/.test(line) || line.length < 10) {
                throw new Error(`Invalid HEX digits on line ${lineNum+1}`);
            }
            // parse length, address, type, data + checksum
            const length = parseInt(line.slice(0, 2), 16);
            const address = parseInt(line.slice(2, 6), 16);
            const recordType = parseInt(line.slice(6, 8), 16);
            let cursor = 8;
            let calcSum = length + ((address >> 8) & 0xFF) + (address & 0xFF) + recordType;

            if (line.length !== (10 + length*2)) {
                throw new Error(`Line length mismatch on line ${lineNum+1}`);
            }

            let endOfFile = false;
            switch (recordType) {
                case 0x00: {
                    // data record
                    const dataBytes = new Uint8Array(length);
                    for (let i = 0; i < length; i++) {
                        const byteVal = parseInt(line.slice(cursor, cursor+2), 16);
                        cursor += 2;
                        dataBytes[i] = byteVal;
                        calcSum += byteVal;
                    }

                    const checkSum = parseInt(line.slice(cursor, cursor+2), 16);
                    calcSum = (calcSum & 0xFF);
                    const total = (calcSum + checkSum) & 0xFF;
                    if (total !== 0) {
                        throw new Error(`Checksum error on line ${lineNum+1}`);
                    }

                    let addr32 = baseAddress + address;
                    addr32 -= offset;  // subtract 0x60000000 for Teensy 4.x

                    if (addr32 < 0) {
                        // data is below the offset (e.g. 0x6000_0000)
                        continue;
                    }

                    // place bytes into blocks
                    let dataIndex = 0;
                    while (dataIndex < dataBytes.length) {
                        let blockIndex = Math.floor(addr32 / blockSize);
                        ensureBlock(blockIndex);

                        const blockStartAddr = blockIndex * blockSize;
                        const withinBlockOffset = addr32 - blockStartAddr;
                        const spaceInBlock = blockSize - withinBlockOffset;
                        const toCopy = Math.min(spaceInBlock, dataBytes.length - dataIndex);

                        // copy data
                        blocks[blockIndex].set(
                            dataBytes.subarray(dataIndex, dataIndex + toCopy),
                            withinBlockOffset
                        );

                        dataIndex += toCopy;
                        addr32 += toCopy;
                    }
                    break;
                }
                case 0x01: {
                    // End of file
                    // parse the checksum
                    for (let i = 0; i < length; i++) {
                        const byteVal = parseInt(line.slice(cursor, cursor+2), 16);
                        cursor += 2;
                        calcSum += byteVal;
                    }
                    const checkSum = parseInt(line.slice(cursor, cursor+2), 16);
                    calcSum = (calcSum & 0xFF);
                    const total = (calcSum + checkSum) & 0xFF;
                    if (total !== 0) {
                        throw new Error(`Checksum error on EOF line ${lineNum+1}`);
                    }
                    // no more data records, we can stop
                    endOfFile = true;
                    break;
                }
                case 0x02: {
                    // Extended Segment Address Record
                    // Usually not used by Teensy 4.x, but let's parse properly
                    const highAddr = parseInt(line.slice(cursor, cursor+4), 16);
                    calcSum += ((highAddr >> 8) & 0xFF) + (highAddr & 0xFF);
                    cursor += 4;
                    const checkSum = parseInt(line.slice(cursor, cursor+2), 16);
                    calcSum = (calcSum & 0xFF);
                    if (((calcSum + checkSum) & 0xFF) !== 0) {
                        throw new Error(`Checksum error on line ${lineNum+1}`);
                    }
                    baseAddress = highAddr * 16; // shift by 4 bits
                    break;
                }
                case 0x04: {
                    // Extended Linear Address Record
                    const upper16 = parseInt(line.slice(cursor, cursor+4), 16);
                    calcSum += ((upper16 >> 8) & 0xFF) + (upper16 & 0xFF);
                    cursor += 4;
                    const checkSum = parseInt(line.slice(cursor, cursor+2), 16);
                    calcSum = (calcSum & 0xFF);
                    if (((calcSum + checkSum) & 0xFF) !== 0) {
                        throw new Error(`Checksum error on line ${lineNum+1}`);
                    }
                    baseAddress = upper16 * 0x10000; // << 16 goes negative from 0x8000 up
                    break;
                }
                default: {
                    // parse the bytes to keep the checksum logic correct
                    for (let i = 0; i < length; i++) {
                        const byteVal = parseInt(line.slice(cursor, cursor+2), 16);
                        cursor += 2;
                        calcSum += byteVal;
                    }
                    const checkSum = parseInt(line.slice(cursor, cursor+2), 16);
                    calcSum = (calcSum & 0xFF);
                    if (((calcSum + checkSum) & 0xFF) !== 0) {
                        throw new Error(`Checksum error on line ${lineNum+1}`);
                    }
                    // ignoring other record types
                    break;
                }
            }
            if (endOfFile) break;
        }

        // Sparse on purpose: the index is the address. (The original returned
        // blocks.filter(b => b), which shifts everything after a gap.)
        return blocks;
    }

    /**
     * Turn the firmware file into 1KB blocks: parse it if it's a .hex,
     * otherwise treat it as a raw binary that starts at the flash base.
     */
    async function buildFirmwareBlocks(firmwareFile, firmwareFileName) {
        if (firmwareFileName.toLowerCase().endsWith('.hex') || looksLikeHex(firmwareFile)) {
            return parseHexToBlocks(firmwareFile, BLOCK_SIZE, FLASH_BASE);
        }
        const pages = [];
        for (let i = 0; i < firmwareFile.length; i += BLOCK_SIZE) {
            const chunk = firmwareFile.slice(i, i + BLOCK_SIZE);
            if (chunk.length < BLOCK_SIZE) {
                // pad to 1024
                const padded = new Uint8Array(BLOCK_SIZE);
                padded.fill(0xFF);
                padded.set(chunk, 0);
                pages.push(padded);
            } else {
                pages.push(chunk);
            }
        }
        return pages;
    }

    function readU32(block, at) {
        return (block[at] | (block[at + 1] << 8) | (block[at + 2] << 16) | (block[at + 3] << 24)) >>> 0;
    }

    /**
     * Check the image before erasing anything. A Teensy 4 image starts with the
     * FlexSPI config block ("FCFB"), whose flash size tells which board it was
     * built for. A Teensy 3.x hex lands below 0x60000000 and would leave nothing
     * to write, so the bootloader would erase the chip for nothing.
     */
    function inspectImage(blocks) {
        const b0 = blocks[0];
        if (!b0 || readU32(b0, 0) !== FCFB_TAG) return { teensy4: false };
        const flashSize = readU32(b0, 0x50);
        const board = BOARDS[flashSize] || null;
        return {
            teensy4: true,
            flashSize,
            board: board ? board.name : null,
            code: board ? board.code : null,
            usedBytes: blocks.length * BLOCK_SIZE,
        };
    }

    // Blocks that get sent: always the first one (it starts the erase), then every
    // block that has data and isn't blank.
    function planBlocks(blocks) {
        const plan = [0];
        for (let i = 1; i < blocks.length; i++) {
            const b = blocks[i];
            if (b && !b.every(x => x === 0xFF)) plan.push(i);
        }
        return plan;
    }

    // [0..2] = address bytes, [3..63] = zero padding, [64..1087] = data
    function buildReport(index, block) {
        const report = new Uint8Array(REPORT_SIZE);
        const addr = index * BLOCK_SIZE;
        report[0] = addr & 0xFF;
        report[1] = (addr >> 8) & 0xFF;
        report[2] = (addr >> 16) & 0xFF;
        if (block) report.set(block, 64);
        else report.fill(0xFF, 64);
        return report;
    }

    /**
     * Flash Teensy firmware:
     *  - WebHID open
     *  - skip 0xFF blocks except first
     *  - send block as [3 address bytes + 61 zero + 1024 data = 1088 total]
     *  - small delay after each block
     *  - final "magic bytes" 0xFF,0xFF,0xFF
     * @param {Uint8Array[]} firmwarePages sparse, indexed by block number
     * @param {HIDDevice} device
     * @param {(progress:number, phase:string)=>void} progressCb
     */
    async function flashFirmware(firmwarePages, device, progressCb) {
        progressCb(0, "erase");
        if (!device.opened) await device.open();

        try {
            const plan = planBlocks(firmwarePages);
            for (let n = 0; n < plan.length; n++) {
                const i = plan[n];
                const success = await sendReportWithRetries(device, buildReport(i, firmwarePages[i]));
                if (!success) {
                    throw new Error(`Block upload failed at block index=${i}`);
                }

                // 1.5s after the first block (erase), 5ms for subsequent
                await sleep(n === 0 ? 1500 : 5);
                progressCb((n + 1) / plan.length, "write");
            }

            // final "magic" = 0xFF,0xFF,0xFF
            const boot = new Uint8Array(REPORT_SIZE);
            boot[0] = 0xFF;
            boot[1] = 0xFF;
            boot[2] = 0xFF;
            await sendReportWithRetries(device, boot);

            await sleep(100);
        } finally {
            await device.close().catch(() => {});
        }
    }

    /**
     * Send a HID report, retrying until it goes through or the time runs out.
     * The first block starts an erase that runs in the background, and while it
     * lasts HalfKay refuses reports right away (NotAllowedError). The erase grows
     * with the firmware that was there before (over 2 s for a 550 KB one), so a
     * fixed number of tries isn't enough; teensy_loader_cli also keeps retrying.
     */
    async function sendReportWithRetries(device, data, budgetMs = RETRY_BUDGET_MS) {
        const until = Date.now() + budgetMs;
        for (let attempt = 1; ; attempt++) {
            try {
                await device.sendReport(0, data);
                if (attempt > 1) console.info(`sendReport went through on attempt ${attempt}`);
                return true;
            } catch (err) {
                if (!device.opened || Date.now() >= until) {
                    console.warn(`sendReport gave up after ${attempt} attempts`, err);
                    return false;
                }
                await sleep(RETRY_GAP_MS);
            }
        }
    }

    // "S face=3 FROWN bright=7 ... temp=46.2 ..." -> { face: "3", faceName: "FROWN", bright: "7", ... }
    function parseStatusLine(line) {
        if (!line.startsWith("S face=")) return null;
        const out = {};
        for (const tok of line.slice(2).split(/\s+/)) {
            const eq = tok.indexOf("=");
            if (eq > 0) out[tok.slice(0, eq)] = tok.slice(eq + 1);
            else if (tok && out.face !== undefined && out.faceName === undefined) out.faceName = tok;
        }
        return out;
    }

    const api = {
        REPO, ASSET_PREFIX, SPECIES, FW_LANGS, HEAD_LANG_CMD, BOARDS, FLASH_BASE, BLOCK_SIZE, REPORT_SIZE,
        assetName, sha256Of, sha256Hex, langFromNavigator, looksLikeHex, parseHexToBlocks, buildFirmwareBlocks,
        inspectImage, planBlocks, buildReport, flashFirmware, sendReportWithRetries, parseStatusLine,
    };
    if (typeof module !== "undefined" && module.exports) module.exports = api;
    if (typeof document === "undefined") return;

    // =====================================================================
    // Page
    // =====================================================================
    const I18N = global.I18N;
    const $ = (id) => document.getElementById(id);
    const hasHid = "hid" in navigator;
    const hasSerial = "serial" in navigator;

    const S = {
        lang: "en",
        fwLang: "en",
        fwLangPicked: false,
        species: "protogen",
        source: "release",
        releases: [],
        relIdx: -1,
        latestIdx: -1,
        fwRelease: null,
        fwLocal: null,
        loadToken: 0,
        device: null,
        busy: false,
        rebooted: false,
        flashed: false,
        quietUntil: 0,
        con: { port: null, open: false, reader: null, pipe: null, partial: "", chain: Promise.resolve() },
        status: null,
        fps: null,
    };
    let T = I18N.en;
    let visor = null;
    const msgs = {};

    function t(key, vars) {
        let s = (key in T) ? T[key] : (I18N.en[key] !== undefined ? I18N.en[key] : key);
        if (vars) {
            s = s.replace(/\{(\w+)\}/g, (m, k) => {
                if (!(k in vars)) return m;
                const v = vars[k];
                return typeof v === "function" ? v() : v;
            });
        }
        return s;
    }

    function errText(e) {
        return (e && (e.message || e.name)) ? String(e.message || e.name) : String(e);
    }

    // Messages are kept as keys so they can be re-rendered in another language.
    function say(area, key, vars, kind, link) {
        msgs[area] = key ? { key, vars, kind: kind || "info", link } : null;
        renderMsg(area);
    }

    function renderMsg(area) {
        const el = $("msg-" + area);
        if (!el) return;
        const m = msgs[area];
        el.textContent = "";
        el.dataset.kind = m ? m.kind : "";
        el.hidden = !m;
        if (!m) return;
        el.append(t(m.key, m.vars));
        if (m.link) {
            const a = document.createElement("a");
            a.href = m.link.href;
            a.textContent = t(m.link.key, m.link.vars);
            a.rel = "noopener";
            el.append(" ", a);
        }
    }

    // ===== Language =====
    function applyLang(lang) {
        S.lang = lang;
        T = I18N[lang];
        document.documentElement.lang = T.htmlLang;
        document.title = T.docTitle;
        const desc = document.querySelector('meta[name="description"]');
        if (desc) desc.content = T.metaDesc;
        document.querySelectorAll("[data-i18n]").forEach(el => { el.textContent = t(el.dataset.i18n); });
        document.querySelectorAll("[data-i18n-attr]").forEach(el => {
            for (const pair of el.dataset.i18nAttr.split(";")) {
                const [attr, key] = pair.split(":");
                el.setAttribute(attr, t(key));
            }
        });
        document.querySelectorAll(".langs button").forEach(b => {
            b.setAttribute("aria-pressed", String(b.dataset.lang === lang));
        });
        try { localStorage.setItem(STORE + "lang", lang); } catch (e) { /* private mode */ }
        if (!S.fwLangPicked) setFwLang(lang, false);
        renderReleaseOptions();
        renderReleaseInfo();
        Object.keys(msgs).forEach(renderMsg);
        renderStatus();
    }

    function setFwLang(lang, picked) {
        if (picked) S.fwLangPicked = true;
        const changed = S.fwLang !== lang;
        S.fwLang = lang;
        document.querySelectorAll('input[name="fwlang"]').forEach(r => { r.checked = r.value === lang; });
        if (changed && S.releases.length) loadReleaseAsset();
    }

    function setSpecies(species, save) {
        const changed = S.species !== species;
        S.species = species;
        document.querySelectorAll('input[name="species"]').forEach(r => { r.checked = r.value === species; });
        if (save) {
            try { localStorage.setItem(STORE + "species", species); } catch (e) { /* private mode */ }
        }
        if (changed && S.releases.length) loadReleaseAsset();
    }

    // ?species=primagen, then the last one picked, then protogen
    function initialSpecies() {
        try {
            const q = new URLSearchParams(location.search).get("species");
            if (SPECIES.includes(q)) return q;
        } catch (e) { /* ignore */ }
        try {
            const saved = localStorage.getItem(STORE + "species");
            if (SPECIES.includes(saved)) return saved;
        } catch (e) { /* private mode */ }
        return SPECIES[0];
    }

    function initialLang() {
        try {
            const q = new URLSearchParams(location.search).get("lang");
            if (q && I18N[q]) return q;
        } catch (e) { /* ignore */ }
        try {
            const saved = localStorage.getItem(STORE + "lang");
            if (saved && I18N[saved]) return saved;
        } catch (e) { /* private mode */ }
        return langFromNavigator(navigator.languages && navigator.languages.length ? navigator.languages : [navigator.language]);
    }

    // ===== GitHub Firmware Fetch =====
    function normalizeReleases(list) {
        return (list || [])
            .filter(r => !r.draft)
            .map(r => ({
                tag: r.tag_name,
                prerelease: !!r.prerelease,
                published: r.published_at || null,
                url: r.html_url || `${REPO_URL}/releases/tag/${encodeURIComponent(r.tag_name)}`,
                assets: (r.assets || [])
                    .filter(a => a.name && a.name.startsWith(ASSET_PREFIX) && a.name.endsWith(".hex"))
                    .map(a => ({
                        name: a.name,
                        id: a.id || null,
                        sha256: sha256Of(a),
                        url: a.browser_download_url ||
                            `${REPO_URL}/releases/download/${encodeURIComponent(r.tag_name)}/${a.name}`,
                    })),
            }))
            .filter(r => r.tag && r.assets.length);
    }

    async function loadMirrorManifest() {
        try {
            const r = await fetch(MIRROR_DIR + "manifest.json", { cache: "no-cache" });
            if (!r.ok) return [];
            const j = await r.json();
            return normalizeReleases(j.releases);
        } catch (e) {
            return [];
        }
    }

    async function loadReleases() {
        say("fw", "loadingReleases");
        const mirrorReq = loadMirrorManifest();
        let list = null;
        let problem = null;
        try {
            const r = await fetch(RELEASES_API, { headers: { Accept: "application/vnd.github+json" } });
            if (r.ok) list = normalizeReleases(await r.json());
            else if (r.status === 404) list = [];
            else if ((r.status === 403 || r.status === 429) && r.headers.get("x-ratelimit-remaining") === "0") problem = { key: "rateLimited" };
            else problem = { key: "releasesError", detail: `HTTP ${r.status}` };
        } catch (e) {
            problem = { key: "releasesError", detail: errText(e) };
        }
        const mirror = await mirrorReq;
        if (list && list.length) {
            // The API may not list digests; the mirror's manifest has them for its copies.
            for (const r of list) {
                const m = mirror.find(x => x.tag === r.tag);
                for (const a of r.assets) {
                    const ma = m && m.assets.find(x => x.name === a.name);
                    if (ma && !a.sha256) a.sha256 = ma.sha256;
                }
            }
        } else if (mirror.length) {
            list = mirror;
            problem = null;
        }
        S.releases = list || [];
        S.latestIdx = S.releases.findIndex(r => !r.prerelease);
        if (S.latestIdx < 0 && S.releases.length) S.latestIdx = 0;
        S.relIdx = S.latestIdx;
        renderReleaseOptions();
        renderReleaseInfo();
        if (S.releases.length) {
            loadReleaseAsset();
        } else {
            // shown on both panels: the page switches to the local file one
            const key = problem ? problem.key : "noReleases";
            const vars = { detail: problem && problem.detail ? problem.detail : "" };
            say("fw", key, vars, "warn");
            if (!S.fwLocal) say("local", key, vars, "warn");
            setSource("local");
        }
    }

    function renderReleaseOptions() {
        const sel = $("release");
        if (!sel) return;
        sel.textContent = "";
        S.releases.forEach((r, i) => {
            const o = document.createElement("option");
            o.value = String(i);
            // the default one can be a pre-release too (when nothing stable is out yet): say so
            const tags = [];
            if (i === S.latestIdx) tags.push(t("latest"));
            if (r.prerelease) tags.push(t("prerelease"));
            o.textContent = tags.length ? `${r.tag} (${tags.join(" · ")})` : r.tag;
            o.selected = i === S.relIdx;
            sel.append(o);
        });
        sel.disabled = !S.releases.length;
    }

    function renderReleaseInfo() {
        const el = $("release-info");
        if (!el) return;
        el.textContent = "";
        const r = S.releases[S.relIdx];
        if (!r) return;
        if (r.published) {
            const d = new Date(r.published);
            if (!isNaN(d)) {
                const date = new Intl.DateTimeFormat(T.htmlLang, { dateStyle: "long" }).format(d);
                el.append(t("publishedOn", { date }), " ");
            }
        }
        const a = document.createElement("a");
        a.href = r.url;
        a.rel = "noopener";
        a.textContent = t("releaseNotes");
        el.append(a);
    }

    // Returns { bytes } or { bad: true } when a copy doesn't match the release's SHA-256.
    async function downloadAsset(rel, asset) {
        const tries = [[MIRROR_DIR + encodeURIComponent(rel.tag) + "/" + asset.name, {}]];
        if (asset.id) {
            // Fails today (the redirect target has no CORS headers); kept in case GitHub adds them.
            tries.push([`https://api.github.com/repos/${REPO}/releases/assets/${asset.id}`,
                { headers: { Accept: "application/octet-stream" } }]);
        }
        let bad = false;
        for (const [url, opts] of tries) {
            try {
                const r = await fetch(url, opts);
                if (!r.ok) continue;
                const bytes = new Uint8Array(await r.arrayBuffer());
                if (!looksLikeHex(bytes)) continue;
                if (asset.sha256 && global.crypto && crypto.subtle && await sha256Hex(bytes) !== asset.sha256) {
                    console.warn(`SHA-256 mismatch: ${url}`);
                    bad = true;
                    continue;
                }
                return { bytes };
            } catch (e) {
                console.info(`Download failed: ${url}`, e);
            }
        }
        return { bad };
    }

    async function loadReleaseAsset() {
        const rel = S.releases[S.relIdx];
        if (!rel) return;
        const lang = S.fwLang;
        const species = S.species;
        const name = assetName(species, lang);
        const asset = rel.assets.find(a => a.name === name);
        const token = ++S.loadToken;
        S.fwRelease = null;
        S.flashed = false;
        updateSteps();
        if (!asset) {
            say("fw", "assetMissing", { lang: () => T.langNames[lang], species: species[0].toUpperCase() + species.slice(1) }, "warn");
            return;
        }
        say("fw", "downloading", { name });
        const { bytes, bad } = await downloadAsset(rel, asset);
        if (token !== S.loadToken) return; // another version/species/language was picked meanwhile
        if (!bytes) {
            if (bad) say("fw", "badChecksum", { name }, "error", { href: asset.url, key: "downloadLink", vars: { name } });
            else say("fw", "downloadBlocked", null, "warn", { href: asset.url, key: "downloadLink", vars: { name } });
            return;
        }
        S.fwRelease = { bytes, name, kb: Math.round(bytes.length / 1024) };
        say("fw", "fwReady", { name, kb: S.fwRelease.kb }, "ok");
        updateSteps();
    }

    // ===== Local file =====
    async function loadLocalFile(file) {
        if (!file) return;
        S.flashed = false;
        try {
            const bytes = new Uint8Array(await file.arrayBuffer());
            if (/\.hex$/i.test(file.name) && !looksLikeHex(bytes)) {
                S.fwLocal = null;
                say("local", "notHex", null, "error");
            } else {
                S.fwLocal = { bytes, name: file.name, kb: Math.round(bytes.length / 1024) };
                say("local", "fwReady", { name: file.name, kb: S.fwLocal.kb }, "ok");
            }
        } catch (e) {
            S.fwLocal = null;
            say("local", "readError", null, "error");
        }
        setSource("local");
        updateSteps();
    }

    function setSource(src) {
        S.source = src;
        document.querySelectorAll('input[name="src"]').forEach(r => { r.checked = r.value === src; });
        $("src-release").hidden = src !== "release";
        $("src-local").hidden = src !== "local";
        updateSteps();
    }

    function currentFw() {
        return S.source === "release" ? S.fwRelease : S.fwLocal;
    }

    // ===== Bootloader (HID) =====
    function isHalfKay(d) {
        return HALFKAY_FILTERS.some(f => d.vendorId === f.vendorId && d.productId === f.productId);
    }

    function isTeensyPort(p) {
        const info = p.getInfo ? p.getInfo() : {};
        return info.usbVendorId === TEENSY_VID;
    }

    function deviceName(d) {
        return d.productName || "Teensy (bootloader)";
    }

    function useDevice(d) {
        S.device = d;
        S.flashed = false;
        say("dev", "devConnected", { name: deviceName(d) }, "ok");
        visor.set("owo");
        updateSteps();
    }

    async function waitForBootloader(ms) {
        if (!hasHid) return null;
        const until = Date.now() + ms;
        while (Date.now() < until) {
            const d = (await navigator.hid.getDevices()).find(isHalfKay);
            if (d) return d;
            await sleep(250);
        }
        return null;
    }

    async function pickSerialPort() {
        const granted = (await navigator.serial.getPorts()).filter(isTeensyPort);
        if (granted.length === 1) return granted[0];
        return navigator.serial.requestPort({ filters: [{ usbVendorId: TEENSY_VID }] });
    }

    // Opening the port at 134 baud sends SET_LINE_CODING(134); Teensyduino's USB
    // stack answers by jumping to the bootloader about 10 ms later (usb.c).
    async function rebootToBootloader() {
        if (!hasSerial) { say("boot", "noSerialApi", null, "warn"); return; }
        if (S.busy) return;
        let port;
        try {
            if (S.con.port) {
                port = S.con.port;
                await closeConsole(true);
            } else {
                port = await pickSerialPort();
            }
        } catch (e) {
            say("boot", "noPortChosen", null, "warn");
            return;
        }
        setBusy(true);
        visor.set("surprised");
        say("boot", "rebooting");
        S.quietUntil = Date.now() + 8000;
        try {
            await port.open({ baudRate: REBOOT_BAUD });
        } catch (e) {
            setBusy(false);
            visor.set("dead", "amber");
            say("boot", "portBusy", { detail: errText(e) }, "error");
            return;
        }
        await sleep(300);
        try { await port.close(); } catch (e) { /* it's already gone */ }
        if (S.con.port === port) S.con.port = null;
        const d = await waitForBootloader(5000);
        setBusy(false);
        S.rebooted = true;
        if (d) {
            useDevice(d);
            say("boot", "bootDetected", null, "ok");
        } else {
            say("boot", "rebootSent", null, "ok");
        }
        updateSteps();
    }

    async function connectTeensy() {
        if (!hasHid || S.busy) return;
        try {
            let d = (await navigator.hid.getDevices()).find(isHalfKay);
            if (!d) {
                const list = await navigator.hid.requestDevice({ filters: HALFKAY_FILTERS });
                d = list[0];
            }
            if (!d) { say("dev", "devNotChosen", null, "warn"); return; }
            useDevice(d);
        } catch (e) {
            say("dev", "devError", { detail: errText(e) }, "error");
        }
    }

    // ===== Upload =====
    async function install() {
        if (S.busy) return;
        const fw = currentFw();
        if (!fw) { say("flash", "needFw", null, "warn"); return; }
        if (!S.device && hasHid) {
            const d = (await navigator.hid.getDevices()).find(isHalfKay);
            if (d) useDevice(d);
        }
        if (!S.device) { say("flash", "needDev", null, "warn"); return; }

        say("flash", "checking");
        let blocks;
        try {
            blocks = await buildFirmwareBlocks(fw.bytes, fw.name);
        } catch (e) {
            say("flash", "parseError", { detail: errText(e) }, "error");
            visor.set("dead");
            return;
        }
        const info = inspectImage(blocks);
        if (!info.teensy4) {
            say("flash", "notTeensy4", null, "error");
            visor.set("dead");
            return;
        }
        const cap = info.code || TEENSY40.code;
        if (info.usedBytes > cap) {
            say("flash", "tooBig", { used: Math.ceil(info.usedBytes / 1024), max: Math.floor(cap / 1024) }, "error");
            visor.set("dead");
            return;
        }
        if (info.board !== TEENSY40.name && !confirm(t("otherBoard", { board: info.board || "?" }))) {
            say("flash", "cancelled", null, "warn");
            return;
        }

        setBusy(true);
        const bar = $("progress");
        bar.value = 0;
        bar.hidden = false;
        visor.setProgress(0);
        say("flash", "erasing");
        try {
            await flashFirmware(blocks, S.device, (p, phase) => {
                bar.value = p;
                visor.setProgress(p);
                if (phase === "write") say("flash", "writing", { pct: Math.round(p * 100) });
            });
            S.quietUntil = Date.now() + 8000;
            S.flashed = true;
            S.device = null; // it reboots into the new firmware
            bar.value = 1;
            say("flash", "done", null, "ok");
            visor.set("happy");
        } catch (e) {
            console.error(e);
            say("flash", "failed", { detail: errText(e) }, "error");
            visor.set("dead");
        } finally {
            setBusy(false);
            updateSteps();
        }
    }

    // ===== Serial console =====
    async function openConsole() {
        if (!hasSerial || S.con.open || S.busy) return;
        let port = S.con.port;
        try {
            if (!port) port = await pickSerialPort();
        } catch (e) {
            say("con", "noPortChosen", null, "warn");
            return;
        }
        try {
            await port.open({ baudRate: CONSOLE_BAUD });
        } catch (e) {
            say("con", "conError", { detail: errText(e) }, "error");
            return;
        }
        const c = S.con;
        c.port = port;
        c.open = true;
        c.partial = "";
        const decoder = new TextDecoderStream();
        c.pipe = port.readable.pipeTo(decoder.writable).catch(() => {});
        c.reader = decoder.readable.getReader();
        say("con", "conOpened", null, "ok");
        updateConsoleUi();
        readLoop(c.reader);
    }

    async function readLoop(reader) {
        try {
            for (;;) {
                const { value, done } = await reader.read();
                if (done) break;
                if (value) onSerialText(value);
            }
        } catch (e) {
            // the port went away; the disconnect handler reports it
        } finally {
            try { reader.releaseLock(); } catch (e) { /* already released */ }
        }
    }

    async function closeConsole(quiet) {
        const c = S.con;
        if (!c.open) return;
        c.open = false;
        try { if (c.reader) await c.reader.cancel(); } catch (e) { /* ignore */ }
        if (c.pipe) await c.pipe; // the port stays locked until the pipe lets go
        try { await c.port.close(); } catch (e) { /* ignore */ }
        c.reader = null;
        c.pipe = null;
        if (!quiet) say("con", "conClosed");
        updateConsoleUi();
    }

    // One command per line, exactly: the firmware ignores anything else on the line.
    function sendLine(line) {
        const c = S.con;
        c.chain = c.chain.then(async () => {
            if (!c.open || !c.port || !c.port.writable) return;
            const w = c.port.writable.getWriter();
            try {
                await w.write(new TextEncoder().encode(line + "\n"));
                logLine("> " + line, "tx");
            } catch (e) {
                say("con", "conError", { detail: errText(e) }, "error");
            } finally {
                w.releaseLock();
            }
        });
        return c.chain;
    }

    function onSerialText(text) {
        const c = S.con;
        const parts = (c.partial + text).split("\n");
        c.partial = parts.pop();
        if (c.partial.length > 4096) c.partial = "";
        for (let line of parts) {
            line = line.replace(/\r$/, "");
            const fps = /^FPS: (\d+)/.exec(line);
            if (fps) {
                S.fps = fps[1];
                scheduleStatus();
                if ($("hide-fps").checked) continue;
            }
            const st = parseStatusLine(line);
            if (st) { S.status = st; scheduleStatus(); }
            logLine(line, /^OK /.test(line) ? "ok" : /^ERR /.test(line) ? "err" : "rx");
        }
    }

    const LOG_MAX = 400;
    let logQueue = [];
    let logRaf = 0;
    function logLine(text, kind) {
        logQueue.push([text, kind]);
        if (!logRaf) logRaf = requestAnimationFrame(flushLog);
    }

    function flushLog() {
        logRaf = 0;
        const el = $("log");
        const stick = el.scrollTop + el.clientHeight >= el.scrollHeight - 12;
        const frag = document.createDocumentFragment();
        for (const [text, kind] of logQueue.splice(0).slice(-LOG_MAX)) {
            const d = document.createElement("div");
            d.className = "l-" + kind;
            d.textContent = text;
            frag.append(d);
        }
        el.append(frag);
        while (el.childElementCount > LOG_MAX) el.firstElementChild.remove();
        if (stick) el.scrollTop = el.scrollHeight;
    }

    let statusRaf = 0;
    function scheduleStatus() {
        if (!statusRaf) statusRaf = requestAnimationFrame(() => { statusRaf = 0; renderStatus(); });
    }

    function renderStatus() {
        const s = S.status || {};
        const set = (id, v) => { const el = $(id); if (el) el.textContent = (v === undefined || v === null || v === "") ? "–" : v; };
        set("st-face", s.face !== undefined ? (s.faceName ? `${s.face} ${s.faceName}` : s.face) : undefined);
        set("st-bright", s.bright);
        set("st-temp", s.temp !== undefined ? `${s.temp} °C` : undefined);
        const lc = FW_LANGS[Number(s.lang)];
        set("st-lang", s.lang === undefined ? undefined : (lc ? T.langNames[lc] : s.lang));
        set("st-fps", S.fps);
    }

    function updateConsoleUi() {
        const open = S.con.open;
        $("con-open").hidden = open;
        $("con-close").hidden = !open;
        $("con-open").disabled = !hasSerial || S.busy || $("con-open").dataset.unsupported === "1";
        document.querySelectorAll("[data-needs-console]").forEach(el => { el.disabled = !open; });
    }

    // ===== Steps =====
    function setBusy(b) {
        S.busy = b;
        document.querySelectorAll("[data-busy-lock]").forEach(el => {
            el.disabled = b || el.dataset.unsupported === "1" || (el.id === "release" && !S.releases.length);
        });
        updateConsoleUi();
    }

    function updateSteps() {
        const done = {
            1: !!currentFw(),
            2: !!S.device || S.rebooted || S.flashed,
            3: !!S.device || S.flashed,
            4: S.flashed,
        };
        let active = 0;
        for (let i = 1; i <= 4; i++) {
            const li = $("step" + i);
            if (!li) continue;
            const state = done[i] ? "done" : (!active ? "active" : "");
            if (!done[i] && !active) active = i;
            li.dataset.state = state;
        }
    }

    // ===== Wiring =====
    function wire() {
        document.querySelectorAll(".langs button").forEach(b => {
            b.addEventListener("click", () => applyLang(b.dataset.lang));
        });
        document.querySelectorAll('input[name="fwlang"]').forEach(r => {
            r.addEventListener("change", () => { if (r.checked) setFwLang(r.value, true); });
        });
        document.querySelectorAll('input[name="species"]').forEach(r => {
            r.addEventListener("change", () => { if (r.checked) setSpecies(r.value, true); });
        });
        document.querySelectorAll('input[name="src"]').forEach(r => {
            r.addEventListener("change", () => { if (r.checked) setSource(r.value); });
        });
        $("release").addEventListener("change", (e) => {
            S.relIdx = Number(e.target.value);
            renderReleaseInfo();
            loadReleaseAsset();
        });

        const input = $("file");
        input.addEventListener("change", () => loadLocalFile(input.files[0]));
        const drop = $("drop");
        ["dragenter", "dragover"].forEach(ev => drop.addEventListener(ev, (e) => {
            e.preventDefault();
            drop.classList.add("over");
        }));
        ["dragleave", "drop"].forEach(ev => drop.addEventListener(ev, () => drop.classList.remove("over")));
        drop.addEventListener("drop", (e) => {
            e.preventDefault();
            const f = e.dataTransfer && e.dataTransfer.files[0];
            if (f) loadLocalFile(f);
        });

        $("btn-reboot").addEventListener("click", rebootToBootloader);
        $("btn-connect").addEventListener("click", connectTeensy);
        $("btn-install").addEventListener("click", install);

        $("con-open").addEventListener("click", openConsole);
        $("con-close").addEventListener("click", () => closeConsole(false));
        document.querySelectorAll("[data-head-lang]").forEach(b => {
            b.dataset.cmd = HEAD_LANG_CMD[b.dataset.headLang];
        });
        document.querySelectorAll("[data-cmd]").forEach(b => {
            b.addEventListener("click", () => sendLine(b.dataset.cmd));
        });
        $("face-set").addEventListener("click", () => {
            const n = Number($("face-n").value);
            if (Number.isInteger(n) && n >= 0 && n <= 255) sendLine("f" + n);
        });
        $("cmd-form").addEventListener("submit", (e) => {
            e.preventDefault();
            const v = $("cmd-input").value.trim();
            if (v) sendLine(v);
            $("cmd-input").value = "";
        });

        document.querySelectorAll("[data-copy]").forEach(b => {
            b.addEventListener("click", async () => {
                try {
                    await navigator.clipboard.writeText($(b.dataset.copy).textContent.trim() + "\n");
                    b.textContent = t("copied");
                    setTimeout(() => { b.textContent = t("copy"); }, 1600);
                } catch (e) { /* clipboard blocked */ }
            });
        });

        if (hasHid) {
            navigator.hid.addEventListener("connect", (e) => {
                if (!S.device && !S.busy && isHalfKay(e.device)) {
                    S.rebooted = true;
                    useDevice(e.device);
                }
            });
            navigator.hid.addEventListener("disconnect", (e) => {
                if (e.device !== S.device) return;
                S.device = null;
                if (Date.now() > S.quietUntil && !S.busy) {
                    say("dev", "devGone", null, "warn");
                    visor.set("default");
                }
                updateSteps();
            });
        }
        if (hasSerial) {
            navigator.serial.addEventListener("disconnect", (e) => {
                const c = S.con;
                if (e.target !== c.port) return;
                const wasOpen = c.open;
                c.open = false;
                c.port = null;
                c.reader = null;
                c.pipe = null;
                if (wasOpen && Date.now() > S.quietUntil) say("con", "conLost", null, "warn");
                updateConsoleUi();
            });
        }
    }

    function compat() {
        const lock = (ids) => ids.forEach(id => { const el = $(id); el.dataset.unsupported = "1"; el.disabled = true; });
        if (!global.isSecureContext) {
            say("compat", "insecure", null, "error");
            lock(["btn-reboot", "btn-connect", "btn-install", "con-open"]);
            visor.set("dead", "amber");
        } else if (!hasHid) {
            say("compat", "noHid", null, "error");
            lock(["btn-reboot", "btn-connect", "btn-install", "con-open"]);
            visor.set("dead", "amber");
        } else if (!hasSerial) {
            say("compat", "noSerial", null, "warn");
            lock(["btn-reboot", "con-open"]);
        }
        const plat = (navigator.userAgentData && navigator.userAgentData.platform) || navigator.platform || "";
        if (/linux/i.test(plat) && !/android/i.test(navigator.userAgent)) $("linux").open = true;
        if (location.hash === "#console") $("console").open = true;
    }

    async function init() {
        visor = new global.Visor($("visor"));
        wire();
        setSpecies(initialSpecies(), false);
        applyLang(initialLang());
        compat();
        setSource("release");
        updateConsoleUi();
        updateSteps();
        loadReleases();
        if (hasHid) {
            try {
                const d = (await navigator.hid.getDevices()).find(isHalfKay);
                if (d) useDevice(d);
            } catch (e) { /* ignore */ }
        }
    }

    if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", init);
    else init();
})(typeof window !== "undefined" ? window : globalThis);
