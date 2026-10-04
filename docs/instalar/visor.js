/*
 * visor.js - LED visor preview for the MochiiTracer · Protogen OS firmware installer.
 *
 * Part of MochiiTracer, a fork of ProtoTracer by coelacant1.
 * Licensed under the GNU AGPL-3.0 (see LICENSE in the repository).
 *
 * Draws the head's two 64x32 HUB75 panels as LED dots. Each face is a real
 * 64x32 frame rendered by the ProtoTracer engine for the right-hand panel seen
 * from the front (nose on the left). The left panel shows its mirror image,
 * the same way the head does it.
 */
(function (global) {
    "use strict";

    const W = 64;          // one panel
    const H = 32;
    const COLS = W * 2;    // both panels
    const GAP = 3;         // gap between panels, in dot pitches
    const PAD_X = 2.6;     // margin inside the visor, in dot pitches
    const PAD_Y = 2.2;

    // 1 bit per LED, row-major, MSB first (base64 of 256 bytes).
    const FACES = {
        default:   "AAAAAAAAAAAAAAAAAAAAAAAAAAAH+AAAAAAAAH//wAAAAAAB///4AAAAAAP///4AAAAAB////4AAAAAH////4AAAAA/////wAAAAB//gAAAAAAAH/AAAAAAAAAfAAAAAAAAABgAAAAAAAAAAAAAAAAAAAAAAAAAAAB8AAAAAAAAAMAAAAAAAAABAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAPAAAAAAAAAHwAAAAAAAAH4AAAAAAPwD+AADgAAH///AAAH/gP///wAAAP///4H4AAAAAf/4AAAAAAAAB4AAAAAAAAAAAAAAAAAA==",
        surprised: "AAAAAAAAAAAAAAAAAAAAAAAAAAAf/gAAAAAAAP//wAAAAAAB///4AAAAAAP///4AAAAAB////8AAAAAH////4AAAAAf////wAAAAB////8AAAAAH///gAAAAAAf/4AAAAAAAB4AJzAAAAAAAAA3MAAAAAAAADMwAAB8AAAAOzAAAMAAAAAJAAABAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAPAAAAAAAAAPgAAAAAAAAH4AAAAAAPgH+AADwAAP///AAAH/Af///gAAAP///wH4AAAAAf/wAAAAAAAAB4AAAAAAAAAAAAAAAAAA==",
        happy:     "AAAAAAAAAAAAAAAAAAAAAAAAAAAA/+AAAAAAAAP/8AAAAAAAD//8AAAAAAAf//8AAAAAAD8AP4AAAAAAfgAfwAAAAAD8AAfwAAAAAfgAA/gAAAAD8AAA8AAAAAPAAAAwAAAAA4AJzAAAAAAAAA3MAAAAAAAADMwAAB8AAAAOzgAAMAAAAAJIAABAAAAAAAAAAAAAAAAAAAAAAAAAAAwAAAAAAAAAfAAAAAAAAAf4AAAAAAAAf/AAAAAAAB//4AAAAAAP//+AAAB//////gAAAH/////wAAAAf////4AAAAB////wAAAAAH///AAAAAAAf/AAAAAAAAAAAAAAAAAAAA==",
        dead:      "AAAAAAAAAAAAAAAAEAAACAAAAAAcAAA4AAAAAB8AAfgAAAAAH8AH+AAAAAAH+B/AAAAAAAD+fwAAAAAAAD/8AAAAAAAAD/AAAAAAAAA//AAAAAAAAP5/AAAAAAAD+B/AAAAAAB/gB/AAAAAAHwAB+AAAAAAcAAB4AB8AABAAAAgAMAAAAAAAAABAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAf//////AAAB//////8AAAH+AAAAAAAAAfwAAAAAAAAB/AAAAAAAAAH4AAAAAAAAAfAAAAAAAAAAAAAAAAAAAAA==",
        owo:       "AAAAAAAAAAAAAAAAAAB/AAAAAAAAAP+AAAAAAAAB/8AAAAAAAAPB4AAAAAAAB4DwAAAAAAAHAHgAAAAAAAcAOAAAAAAABwA4AAAAAAAHADgAAAAAAAcAeAAAAAAABwB4AAAAAAADgHAAAAAAAAPj4AAAAAAAAf/AAB8AAAAB/4AAMAAAAAAPAABAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAQAAAAAAAAABAAAAAAAAAAGABgAAAAAAAMAGAAAAAAAAwA4AAAAAAADgDgAAAAAAAHg8AAAAAAAAf/gAAAAAAAAf8AAAAAAAAAOAAAAAAAAAAAAAAAAAAAA==",
    };

    const COLORS = {
        cyan: "#3ce7ef",
        amber: "#ffb547",
        red: "#ff5468",
    };

    function decodePanel(b64) {
        const bin = atob(b64);
        const px = new Uint8Array(W * H);
        for (let i = 0; i < W * H; i++) {
            px[i] = (bin.charCodeAt(i >> 3) >> (7 - (i & 7))) & 1;
        }
        return px;
    }

    // Right panel as rendered, left panel mirrored, like HUB75Controller::Display.
    function bothPanels(panel) {
        const out = new Uint8Array(COLS * H);
        for (let y = 0; y < H; y++) {
            for (let x = 0; x < W; x++) {
                const v = panel[y * W + x];
                out[y * COLS + W + x] = v;
                out[y * COLS + (W - 1 - x)] = v;
            }
        }
        return out;
    }

    // The default face's eyes plus a bar that fills from the nose outwards.
    function progressPanel(eyes, p) {
        const px = new Uint8Array(W * H);
        for (let i = 0; i < W * 13; i++) px[i] = eyes[i];
        const x0 = 3, x1 = 60, y0 = 21, y1 = 28;
        for (let x = x0; x <= x1; x++) { px[y0 * W + x] = 1; px[y1 * W + x] = 1; }
        for (let y = y0; y <= y1; y++) { px[y * W + x0] = 1; px[y * W + x1] = 1; }
        const fill = Math.round((x1 - x0 - 3) * Math.max(0, Math.min(1, p)));
        for (let y = y0 + 2; y <= y1 - 2; y++) {
            for (let x = x0 + 2; x < x0 + 2 + fill; x++) px[y * W + x] = 1;
        }
        return px;
    }

    // Closed eyes: keep only the lowest lit dot of each eye column.
    function blinkPanel(panel) {
        const px = panel.slice();
        for (let x = 20; x < W; x++) {
            let lowest = -1;
            for (let y = 0; y < 13; y++) if (panel[y * W + x]) lowest = y;
            for (let y = 0; y < 13; y++) px[y * W + x] = (y === lowest) ? 1 : 0;
        }
        return px;
    }

    class Visor {
        constructor(canvas) {
            this.canvas = canvas;
            this.ctx = canvas.getContext("2d");
            this.glow = document.createElement("canvas");
            this.faces = {};
            for (const k of Object.keys(FACES)) this.faces[k] = decodePanel(FACES[k]);
            this.state = "default";
            this.color = COLORS.cyan;
            this.progress = 0;
            this.frame = bothPanels(this.faces.default);
            this.from = this.frame;
            this.rand = new Float32Array(COLS * H).map(() => Math.random());
            this.t0 = 0;
            this.anim = 0;
            this.reduced = global.matchMedia && global.matchMedia("(prefers-reduced-motion: reduce)").matches;
            this.blinkTimer = 0;
            const ro = new ResizeObserver(() => this.resize());
            ro.observe(canvas);
            this.resize();
            this.scheduleBlink();
        }

        resize() {
            const dpr = global.devicePixelRatio || 1;
            const w = Math.max(1, Math.round(this.canvas.clientWidth * dpr));
            const h = Math.max(1, Math.round(w * (H + 2 * PAD_Y) / (COLS + GAP + 2 * PAD_X)));
            if (this.canvas.width !== w || this.canvas.height !== h) {
                this.canvas.width = w;
                this.canvas.height = h;
                this.glow.width = w;
                this.glow.height = h;
            }
            this.draw(this.frame);
        }

        target() {
            if (this.state === "progress") return bothPanels(progressPanel(this.faces.default, this.progress));
            return bothPanels(this.faces[this.state] || this.faces.default);
        }

        // name: default | surprised | owo | happy | dead | progress
        // tone: cyan | amber | red
        set(name, tone) {
            const color = COLORS[tone || (name === "dead" ? "red" : "cyan")];
            if (name === this.state && color === this.color) return;
            this.state = name;
            this.color = color;
            this.transition(this.target());
        }

        setProgress(p) {
            this.progress = p;
            if (this.state !== "progress") { this.set("progress"); return; }
            this.frame = this.target();
            this.draw(this.frame);
        }

        transition(next) {
            if (this.reduced) {
                this.frame = next;
                this.draw(next);
                return;
            }
            this.from = this.frame;
            this.frame = next;
            this.t0 = performance.now();
            cancelAnimationFrame(this.anim);
            const step = (now) => {
                const t = Math.min(1, (now - this.t0) / 240);
                const mix = new Uint8Array(COLS * H);
                for (let i = 0; i < mix.length; i++) mix[i] = this.rand[i] < t ? this.frame[i] : this.from[i];
                this.draw(mix);
                if (t < 1) this.anim = requestAnimationFrame(step);
            };
            this.anim = requestAnimationFrame(step);
        }

        scheduleBlink() {
            if (this.reduced) return;
            clearTimeout(this.blinkTimer);
            this.blinkTimer = setTimeout(() => {
                if (this.state === "default" && !document.hidden) {
                    this.draw(bothPanels(blinkPanel(this.faces.default)));
                    setTimeout(() => { if (this.state === "default") this.draw(this.frame); }, 130);
                }
                this.scheduleBlink();
            }, 3500 + Math.random() * 3500);
        }

        draw(frame) {
            const { ctx, canvas, glow } = this;
            const w = canvas.width, h = canvas.height;
            const pitch = w / (COLS + GAP + 2 * PAD_X);
            const r = pitch * 0.36;
            const ox = PAD_X * pitch + pitch / 2;
            const oy = (h - H * pitch) / 2 + pitch / 2;
            const cx = (x) => ox + (x + (x >= W ? GAP : 0)) * pitch;

            ctx.clearRect(0, 0, w, h);
            ctx.fillStyle = "rgba(150, 180, 200, 0.075)";
            ctx.beginPath();
            for (let y = 0; y < H; y++) {
                for (let x = 0; x < COLS; x++) {
                    if (frame[y * COLS + x]) continue;
                    ctx.moveTo(cx(x) + r, oy + y * pitch);
                    ctx.arc(cx(x), oy + y * pitch, r, 0, Math.PI * 2);
                }
            }
            ctx.fill();

            const g = glow.getContext("2d");
            g.clearRect(0, 0, w, h);
            g.fillStyle = this.color;
            g.beginPath();
            for (let y = 0; y < H; y++) {
                for (let x = 0; x < COLS; x++) {
                    if (!frame[y * COLS + x]) continue;
                    g.moveTo(cx(x) + r * 1.15, oy + y * pitch);
                    g.arc(cx(x), oy + y * pitch, r * 1.15, 0, Math.PI * 2);
                }
            }
            g.fill();
            ctx.save();
            ctx.globalAlpha = 0.85;
            ctx.filter = `blur(${Math.max(1, pitch * 0.9)}px)`;
            ctx.drawImage(glow, 0, 0);
            ctx.restore();
            ctx.drawImage(glow, 0, 0);
        }
    }

    global.Visor = Visor;
})(typeof window !== "undefined" ? window : globalThis);
