/* motion.js: interactive extras. Adds animations only; it never changes your data or logic. */
(function () {
  var calm = window.matchMedia && matchMedia("(prefers-reduced-motion: reduce)").matches;

  // load the animation styles
  var l = document.createElement("link");
  l.rel = "stylesheet"; l.href = "motion.css";
  document.head.appendChild(l);

  // after the first moment, stop the load-in animation so live updates do not replay it
  setTimeout(function () { document.body.classList.add("loaded"); }, 1800);

  // ripple on buttons
  document.addEventListener("pointerdown", function (e) {
    if (calm || !e.target.closest) return;
    var b = e.target.closest(".btn,nav button,.demo button");
    if (!b) return;
    var r = b.getBoundingClientRect(), s = Math.max(r.width, r.height) * 2, sp = document.createElement("span");
    sp.className = "ripple";
    sp.style.cssText = "width:" + s + "px;height:" + s + "px;left:" + (e.clientX - r.left - s / 2) + "px;top:" + (e.clientY - r.top - s / 2) + "px";
    b.appendChild(sp);
    setTimeout(function () { sp.remove(); }, 650);
  });

  // spotlight follows the cursor on shelter cards
  document.addEventListener("pointermove", function (e) {
    if (!e.target.closest) return;
    var c = e.target.closest(".shelter");
    if (!c) return;
    var r = c.getBoundingClientRect();
    c.style.setProperty("--mx", (e.clientX - r.left) + "px");
    c.style.setProperty("--my", (e.clientY - r.top) + "px");
  });

  // totals count up when their value changes
  document.querySelectorAll(".summary b").forEach(function (el) {
    el.dataset.mine = el.textContent;
    new MutationObserver(function () {
      if (calm || el.textContent === el.dataset.mine) return;
      var to = parseInt(el.textContent, 10);
      if (isNaN(to)) return;
      var from = parseInt(el.dataset.mine, 10) || 0, t0 = performance.now();
      cancelAnimationFrame(el._r);
      var put = function (v) { el.textContent = v; el.dataset.mine = String(v); };
      put(from);
      (function step(t) {
        var k = Math.min(1, (t - t0) / 700);
        put(Math.round(from + (to - from) * k));
        if (k < 1) el._r = requestAnimationFrame(step);
      })(t0);
    }).observe(el, { childList: true, characterData: true });
  });

  // flash a shelter card when its count changes
  var grid = document.getElementById("grid"), seen = {}, prevPct = {}, prevLab = {};
  if (grid && !calm) {
    new MutationObserver(function () {
      grid.querySelectorAll(".shelter[data-id]").forEach(function (c) {
        var n = c.querySelector(".count");
        if (!n) return;
        var v = parseInt(n.textContent, 10), id = c.dataset.id;
        if (id in seen && seen[id] !== v) {
          c.classList.add("flash");
          setTimeout(function () { c.classList.remove("flash"); }, 1200);
        }
        var bar = c.querySelector(".bar i"), badge = c.querySelector(".badge");
        var pct = bar ? bar.style.width : "", lab = badge ? badge.textContent : "";
        if (id in seen) {
          if (bar && prevPct[id] && prevPct[id] !== pct) {   // slide the bar from its old length
            bar.style.width = prevPct[id];
            requestAnimationFrame(function () { requestAnimationFrame(function () { bar.style.width = pct; }); });
          }
          if (badge && prevLab[id] && prevLab[id] !== lab) badge.classList.add("pop");
        }
        prevPct[id] = pct; prevLab[id] = lab;
        seen[id] = v;
      });
    }).observe(grid, { childList: true });
  }

  // LIVE / OFFLINE indicator from the Firebase connection
  try {
    if (window.firebase && firebase.apps && firebase.apps.length) {
      firebase.database().ref(".info/connected").on("value", function (s) {
        document.body.dataset.live = s.val() ? "1" : "0";
      });
    }
  } catch (e) {}

  // a new help request drops into the admin list
  var rl = document.getElementById("list"), known = {}, start = Date.now();
  if (rl && !calm) {
    new MutationObserver(function () {
      rl.querySelectorAll(".card").forEach(function (c) {
        var b = c.querySelector("button[data-id]");
        if (!b) return;
        if (!(b.dataset.id in known) && Date.now() - start > 4000) c.classList.add("arrive");
        known[b.dataset.id] = 1;
      });
    }).observe(rl, { childList: true });
  }
})();
