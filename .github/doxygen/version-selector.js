// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: CC-BY-4.0
//
// Mike-style version selector for the Doxygen-generated MXL API reference.
//
// Activated on pages served under /<site>/api/<version>/..., where:
//   - <version> is either a real published version directory (e.g. "main",
//     "v1.1.0") or the "latest" alias.
//
// Reads the per-site /<site>/api/versions.json manifest (written by
// .github/workflows/doxy-docs.yml) and renders a <select> in the doxygen
// title area. Changing the selection navigates to the same sub-page in the
// chosen version, falling back to that version's index when the same page
// doesn't exist there.

(function () {
  'use strict';

  var path = window.location.pathname;
  // Match "<anything>/api/<version>/<subpath>"; the subpath may be empty
  // (e.g. ".../api/v1.1.0/").
  var m = path.match(/^(.*\/api)\/([^/]+)\/(.*)$/);
  if (!m) return;

  var apiBase = m[1];          // e.g. "/mxl/api"
  var currentSlug = m[2];      // e.g. "v1.1.0" or "latest"
  var subPath = m[3] || '';    // page-relative path within the version

  fetch(apiBase + '/versions.json', { cache: 'no-cache' })
    .then(function (r) { return r.ok ? r.json() : Promise.reject(r.status); })
    .then(render)
    .catch(function (err) {
      // Don't break the page if the manifest is missing or unreadable.
      // eslint-disable-next-line no-console
      console.warn('mxl version selector: ' + err);
    });

  function render(versions) {
    if (!Array.isArray(versions) || versions.length === 0) return;

    // Figure out which manifest entry matches the URL we're on, so we can
    // pre-select it. Match by version, then by alias.
    var selectedKey = null;
    for (var i = 0; i < versions.length; i++) {
      var v = versions[i];
      if (v.version === currentSlug) { selectedKey = v.version; break; }
      if (Array.isArray(v.aliases) && v.aliases.indexOf(currentSlug) !== -1) {
        selectedKey = v.version; break;
      }
    }

    var wrapper = document.createElement('div');
    wrapper.className = 'mxl-version-selector';

    var label = document.createElement('label');
    label.htmlFor = 'mxl-version-select';
    label.textContent = 'Version:';

    var select = document.createElement('select');
    select.id = 'mxl-version-select';

    // If we landed on .../api/latest/... but the manifest doesn't list a
    // version literally named "latest" (it shouldn't), still expose "latest"
    // as a synthetic option so the dropdown reflects the URL.
    var hasLatestSlug = versions.some(function (v) { return v.version === 'latest'; });
    if (!hasLatestSlug && currentSlug === 'latest') {
      var optL = document.createElement('option');
      optL.value = 'latest';
      optL.textContent = 'latest';
      optL.selected = true;
      select.appendChild(optL);
    }

    versions.forEach(function (v) {
      var opt = document.createElement('option');
      opt.value = v.version;
      var aliasLabel =
        (Array.isArray(v.aliases) && v.aliases.length)
          ? ' (' + v.aliases.join(', ') + ')'
          : '';
      opt.textContent = (v.title || v.version) + aliasLabel;
      if (selectedKey === v.version) opt.selected = true;
      select.appendChild(opt);
    });

    select.addEventListener('change', function () {
      var dest = apiBase + '/' + select.value + '/' + subPath;
      // Probe the same sub-page in the target version; if missing, fall back
      // to that version's index. This handles renamed or removed pages
      // gracefully.
      fetch(dest, { method: 'HEAD' }).then(function (r) {
        window.location.href = r.ok
          ? dest
          : apiBase + '/' + select.value + '/';
      }).catch(function () {
        window.location.href = apiBase + '/' + select.value + '/';
      });
    });

    wrapper.appendChild(label);
    wrapper.appendChild(select);

    // Insert into doxygen's #titlearea when present (works with both the
    // default doxygen template and doxygen-awesome-css). Otherwise pin to
    // the top of the page so it's at least visible.
    var titleArea = document.getElementById('titlearea');
    if (titleArea) {
      titleArea.appendChild(wrapper);
    } else {
      document.body.insertBefore(wrapper, document.body.firstChild);
    }
  }
})();
