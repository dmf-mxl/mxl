// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: CC-BY-4.0
//
// Adds a "← MXL Documentation" link back to the main (zensical) docs site to
// every page of the Doxygen-generated API reference.
//
// The API reference is published under /<site>/api/<version>/... on the same
// GitHub Pages site as the main documentation, so the site root is whatever
// comes before "/api/<version>/" in the current URL. We derive that at
// runtime rather than hardcoding a repo/site name here.
//
// Loaded with `defer` from doxygen's customised HTML header (see
// .github/workflows/doxy-docs.yml), so #titlearea is already parsed by the
// time this runs.

(function () {
  'use strict';

  // Match "<site>/api/<version>/..." and capture <site>. If we're not on
  // an API-reference page (unlikely, since this script only ships inside
  // the doxygen output), do nothing.
  var m = window.location.pathname.match(/^(.*?)\/api\/[^/]+\//);
  if (!m) return;

  var siteRoot = m[1] + '/';   // e.g. "/mxl/"

  var link = document.createElement('a');
  link.className = 'mxl-main-docs-link';
  link.href = siteRoot;
  link.textContent = '\u2190 MXL Documentation';
  link.title = 'Back to the main MXL documentation site';

  // Prepend so reading order is: "back link | project title | version".
  // Fall back to the top of <body> if #titlearea is missing (e.g. after a
  // future doxygen template change).
  var titleArea = document.getElementById('titlearea');
  if (titleArea) {
    titleArea.insertBefore(link, titleArea.firstChild);
  } else {
    document.body.insertBefore(link, document.body.firstChild);
  }
})();
