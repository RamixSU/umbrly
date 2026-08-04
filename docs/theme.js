// Тема (тёмная/светлая) и акцентный цвет верхнего меню. Применяется как можно
// раньше (см. inline-вызов applyStoredTheme() в <head> index.html, до отрисовки
// body) — иначе при перезагрузке страницы был бы заметен "мигающий" переход
// от дефолтной палитры к сохранённой.
(function () {
  "use strict";

  var STORAGE_THEME = "umbrly-docs-theme";
  var STORAGE_ACCENT = "umbrly-docs-accent";
  var ACCENTS = ["rose", "red", "orange", "green", "blue", "purple"];

  function applyStoredTheme() {
    var theme = localStorage.getItem(STORAGE_THEME) || "dark";
    var accent = localStorage.getItem(STORAGE_ACCENT) || "rose";
    document.documentElement.setAttribute("data-theme", theme);
    document.documentElement.setAttribute("data-accent", accent);
  }

  // Вызывается синхронно из inline-скрипта в <head>, до парсинга остального body.
  window.__umbrlyApplyStoredTheme = applyStoredTheme;

  function setTheme(theme) {
    localStorage.setItem(STORAGE_THEME, theme);
    document.documentElement.setAttribute("data-theme", theme);
    updateToggleIcon();
  }

  function setAccent(accent) {
    localStorage.setItem(STORAGE_ACCENT, accent);
    document.documentElement.setAttribute("data-accent", accent);
    updateAccentPressedState();
  }

  function currentTheme() {
    return document.documentElement.getAttribute("data-theme") || "dark";
  }

  function updateToggleIcon() {
    var btn = document.getElementById("theme-toggle");
    if (!btn) return;
    var dark = currentTheme() === "dark";
    btn.textContent = dark ? "☀" : "☽"; // солнце / полумесяц
    btn.setAttribute("aria-label", dark ? "Включить светлую тему" : "Включить тёмную тему");
  }

  function updateAccentPressedState() {
    var accent = document.documentElement.getAttribute("data-accent");
    document.querySelectorAll(".accent-swatch").forEach(function (btn) {
      btn.setAttribute("aria-pressed", String(btn.dataset.accent === accent));
    });
  }

  document.addEventListener("DOMContentLoaded", function () {
    var toggle = document.getElementById("theme-toggle");
    if (toggle) {
      toggle.addEventListener("click", function () {
        setTheme(currentTheme() === "dark" ? "light" : "dark");
      });
      updateToggleIcon();
    }

    var picker = document.getElementById("accent-picker");
    if (picker) {
      ACCENTS.forEach(function (name) {
        var btn = document.createElement("button");
        btn.type = "button";
        btn.className = "accent-swatch";
        btn.dataset.accent = name;
        // Каждая кнопка красится в свой цвет напрямую (не через общую переменную
        // --accent, которая отражает уже выбранный акцент) — иначе все точки
        // были бы одного текущего цвета вместо палитры на выбор.
        btn.style.setProperty("--swatch-color", accentPreviewColor(name));
        btn.setAttribute("aria-label", "Акцент: " + name);
        btn.addEventListener("click", function () { setAccent(name); });
        picker.appendChild(btn);
      });
      updateAccentPressedState();
    }
  });

  // Цвета для превью точек-переключателей — продублированы из style.css
  // (html[data-accent="..."] переменные), чтобы кнопки показывали свой цвет
  // независимо от того, какой акцент выбран сейчас.
  function accentPreviewColor(name) {
    switch (name) {
      case "rose": return "#d3869b";
      case "red": return "#ea6962";
      case "orange": return "#e78a4e";
      case "green": return "#a9b665";
      case "blue": return "#7daea3";
      case "purple": return "#b16286";
      default: return "#d3869b";
    }
  }
})();
