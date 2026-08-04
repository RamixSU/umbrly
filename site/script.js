(function () {
  "use strict";

  var reduceMotion = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  /* ---------- Hero console: typed command + output ---------- */

  var body = document.getElementById("hero-body");
  if (body) {
    var PROMPT = "C:\\projects\\umbrly>";
    var COMMAND = "umbrly.exe examples\\hello.umb";
    var OUTPUT_LINES = ["Hello World", "67 + 67 = 134"];

    function el(tag, cls, text) {
      var n = document.createElement(tag);
      if (cls) n.className = cls;
      if (text !== undefined) n.textContent = text;
      return n;
    }

    function renderStatic() {
      body.textContent = "";
      var l1 = el("div");
      l1.appendChild(el("span", "prompt", PROMPT));
      l1.appendChild(el("span", "ln-cmd", " " + COMMAND));
      body.appendChild(l1);
      OUTPUT_LINES.forEach(function (line) {
        body.appendChild(el("div", "ln-out", line));
      });
      var l3 = el("div");
      l3.appendChild(el("span", "prompt", PROMPT));
      var cur = el("span", "cursor");
      cur.textContent = "";
      l3.appendChild(cur);
      body.appendChild(l3);
    }

    function runTypedSequence() {
      body.textContent = "";
      var cmdLine = el("div");
      var promptSpan = el("span", "prompt", PROMPT);
      var typedSpan = el("span", "ln-cmd");
      var caret = el("span", "cursor");
      cmdLine.appendChild(promptSpan);
      cmdLine.appendChild(document.createTextNode(" "));
      cmdLine.appendChild(typedSpan);
      cmdLine.appendChild(caret);
      body.appendChild(cmdLine);

      var i = 0;
      function typeChar() {
        if (i < COMMAND.length) {
          typedSpan.textContent += COMMAND[i];
          i++;
          setTimeout(typeChar, 26 + Math.random() * 30);
        } else {
          caret.remove();
          setTimeout(showOutput, 260);
        }
      }

      var lineIdx = 0;
      function showOutput() {
        if (lineIdx < OUTPUT_LINES.length) {
          body.appendChild(el("div", "ln-out", OUTPUT_LINES[lineIdx]));
          lineIdx++;
          setTimeout(showOutput, 180);
        } else {
          var l3 = el("div");
          l3.appendChild(el("span", "prompt", PROMPT));
          l3.appendChild(el("span", "cursor"));
          body.appendChild(l3);
        }
      }

      setTimeout(typeChar, 300);
    }

    if (reduceMotion) renderStatic();
    else runTypedSequence();
  }

  /* ---------- Syntax annotations: hover/focus highlights matching code tokens ---------- */

  var annotations = document.querySelectorAll(".annotation[data-target]");
  annotations.forEach(function (item) {
    var tag = item.getAttribute("data-target");
    var tokens = document.querySelectorAll('[data-tag="' + tag + '"]');

    function activate() {
      item.classList.add("is-active");
      tokens.forEach(function (t) { t.classList.add("hl"); });
    }
    function deactivate() {
      item.classList.remove("is-active");
      tokens.forEach(function (t) { t.classList.remove("hl"); });
    }

    item.addEventListener("mouseenter", activate);
    item.addEventListener("mouseleave", deactivate);
    item.addEventListener("focus", activate);
    item.addEventListener("blur", deactivate);
  });

  /* ---------- Copy-to-clipboard command chips ---------- */

  var chips = document.querySelectorAll(".cmd-chip[data-copy]");
  chips.forEach(function (chip) {
    var label = chip.querySelector(".cmd-copy");
    var defaultLabel = label ? label.textContent : "";
    chip.addEventListener("click", function () {
      var text = chip.getAttribute("data-copy");
      var done = function () {
        chip.classList.add("copied");
        if (label) label.textContent = "скопировано";
        setTimeout(function () {
          chip.classList.remove("copied");
          if (label) label.textContent = defaultLabel;
        }, 1400);
      };
      if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(text).then(done, done);
      } else {
        done();
      }
    });
  });
})();
