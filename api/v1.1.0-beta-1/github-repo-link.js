// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: CC-BY-4.0
//
// Adds a "GitHub" link to every page of the Doxygen-generated API reference,
// pointing at the source repository the docs were built from.
//
// Unlike main-docs-link.js (whose target can be derived from the current URL),
// the GitHub repo isn't recoverable at runtime --- a Pages site may sit behind
// a custom domain that has no relationship to the github.com slug. So the URL
// is baked in at build time by doxy-docs.yml, which substitutes the
// https://github.com/dmf-mxl/mxl placeholder below with ${GITHUB_SERVER_URL}/${GITHUB_REPOSITORY}
// before doxygen copies this file into the output tree.
//
// Loaded with `defer` from doxygen's customised HTML header, so #titlearea is
// already parsed by the time this runs.

(function () {
  'use strict';

  var repoUrl = 'https://github.com/dmf-mxl/mxl';

  var link = document.createElement('a');
  link.className = 'mxl-github-repo-link';
  link.href = repoUrl;
  link.textContent = 'GitHub';
  link.title = 'Source repository on GitHub';
  link.target = '_blank';
  link.rel = 'noopener noreferrer';

  // Append so this lands on the far right of the title area, after the
  // version selector. Left cluster (main-docs-link) and right cluster
  // (version + GitHub) mirror common doc-site conventions.
  var titleArea = document.getElementById('titlearea');
  if (titleArea) {
    titleArea.appendChild(link);
  } else {
    document.body.appendChild(link);
  }
})();
