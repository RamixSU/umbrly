// Рендер справочника функций (группировка по категориям из data.js) и живой
// поиск/фильтр по имени, сигнатуре и описанию. Без зависимостей.
(function () {
  "use strict";

  const funcGroupsEl = document.getElementById("func-groups");
  const funcNavEl = document.getElementById("func-nav");
  const searchInput = document.getElementById("search");
  const searchCountEl = document.getElementById("search-count");
  const noResultsEl = document.getElementById("no-results");

  // Бейджи с количеством функций считаем от длины BUILTINS, а не хардкодим —
  // иначе цифра в разметке протухает при каждом добавлении новой функции.
  document.querySelectorAll("#func-count-badge, #func-count-badge-2").forEach((el) => {
    el.textContent = BUILTINS.length;
  });

  // Сохраняем порядок первого появления категории в data.js.
  const categories = [];
  const byCategory = new Map();
  BUILTINS.forEach((item) => {
    if (!byCategory.has(item.category)) {
      byCategory.set(item.category, []);
      categories.push(item.category);
    }
    byCategory.get(item.category).push(item);
  });

  function slugify(text) {
    return "cat-" + text.toLowerCase().replace(/[^a-zа-я0-9]+/gi, "-").replace(/^-+|-+$/g, "");
  }

  // ---------- Рендер справочника ----------
  categories.forEach((cat) => {
    const items = byCategory.get(cat);
    const catId = slugify(cat);

    const section = document.createElement("div");
    section.className = "func-category";
    section.id = catId;

    const title = document.createElement("h3");
    title.className = "func-category-title";
    title.textContent = `${cat} (${items.length})`;
    section.appendChild(title);

    const list = document.createElement("div");
    list.className = "func-list";

    items.forEach((item) => {
      const row = document.createElement("div");
      row.className = "func-entry";
      row.id = "fn-" + item.name;
      row.dataset.name = item.name.toLowerCase();
      row.dataset.doc = (item.signature + " " + item.doc).toLowerCase();

      const sig = document.createElement("div");
      sig.className = "func-sig";
      sig.textContent = item.signature;

      const doc = document.createElement("div");
      doc.className = "func-doc";
      doc.textContent = item.doc;

      row.appendChild(sig);
      row.appendChild(doc);
      list.appendChild(row);
    });

    section.appendChild(list);
    funcGroupsEl.appendChild(section);

    const navLink = document.createElement("a");
    navLink.href = "#" + catId;
    navLink.className = "is-category";
    navLink.textContent = `${cat} (${items.length})`;
    funcNavEl.appendChild(navLink);
  });

  // ---------- Поиск/фильтр ----------
  function escapeRegExp(s) {
    return s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  }

  // textContent всегда отдаёт чистый текст без разметки, даже если до этого
  // сюда писали innerHTML с <mark> — поэтому можно каждый раз брать textContent
  // заново как источник и не бояться накопления тегов при повторном наборе.
  function highlight(el, query) {
    const raw = el.textContent;
    if (!query) {
      el.textContent = raw;
      return;
    }
    const re = new RegExp("(" + escapeRegExp(query) + ")", "ig");
    el.innerHTML = raw.replace(re, "<mark>$1</mark>");
  }

  function applyFilter() {
    const query = searchInput.value.trim().toLowerCase();
    let visibleCount = 0;

    document.querySelectorAll(".func-entry").forEach((row) => {
      const matches = !query || row.dataset.name.includes(query) || row.dataset.doc.includes(query);
      row.classList.toggle("is-hidden", !matches);
      if (matches) {
        visibleCount++;
        highlight(row.querySelector(".func-sig"), query);
      }
    });

    document.querySelectorAll(".func-category").forEach((section) => {
      const anyVisible = section.querySelector(".func-entry:not(.is-hidden)");
      section.hidden = !anyVisible;
    });

    noResultsEl.hidden = visibleCount !== 0;
    searchCountEl.textContent = query ? `Найдено: ${visibleCount}` : "";
  }

  searchInput.addEventListener("input", applyFilter);
})();
